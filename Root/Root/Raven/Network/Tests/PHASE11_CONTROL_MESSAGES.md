# Phase 11 — 制御メッセージCodecとLoopback

初期版は通信層が存在しない段階のCPU試験導入。続いてWindows IPv4 TCPのLoopback専用Transportを追加した。
配送保証、暗号化、認証、Replay対策は提供しない。Data層へNetwork / Socket依存は追加しない。
PingとSetReadyは試験Protocolの許可リストであり、実運用Lobbyや汎用RPC機能ではない。

## Wire契約

MessagePack Mapは次の4フィールドだけを持つ。各payloadも下記の1フィールドだけを許可する。
未知・欠落・重複キー、不正UTF-8、Extension、不正型は拒否する。

| フィールド | 契約 |
| --- | --- |
| protocolVersion | 整数1のみ。Version変更は旧受信側で拒否する。 |
| messageType | 整数1 = Ping、整数2 = SetReady。それ以外は拒否する。 |
| requestId | 1〜UInt64最大値。関連付け用であり、認証・重複排除ではない。 |
| payload | Pingは`{"nonce": UInt64}`、SetReadyは`{"ready": Bool}`。 |

整数はInt64 / UInt64の非負範囲だけを受理し、Float64 / Bool / Stringは変換しない。
nonceは0〜UInt64最大値。readyはBoolのみ。送信Writerは既存の決定的MessagePackを使う。
ProtocolVersionと型検証の成功後だけ、一時値から呼び出し側の出力へCommitする。
Decodeは所有Documentを作り、Queueには所有する整数・Boolだけを入れる。
DataElementや入力string_viewを呼び出し後に保持しない。

StreamはMessagePack長（Prefixを除く）を4 byte big-endianで先行させる。
0長と4,096 byte超はPrefix受理時にBody確保前に拒否する。
受信Chunkの境界はFrame境界ではなく、1 byte分割・複数Frame結合とも処理する。
Pendingには最大1 FrameのBodyと4 byte Prefixだけを保持し、Chunk全体はコピーしない。
DatagramはPrefixなしでMessagePackを1個だけ持ち、空・1,200 byte超・連結Messageは拒否する。
サイズ上限はこの試験Protocolの選択であり、経路MTUや断片化回避を保証する値ではない。
Parserは最大32 Token、深さ4、各Container 8要素に制限する。

## Queue・Thread・SessionのLifetime

`ControlMessageReceiver`は1接続/Session用。Transportは生成時に固定し、混在利用を拒否する。
mutexでReceive / TryPop / Disconnect / 診断取得を直列化する。
WorkerはReceiveで受信値を検証・Queue投入し、Main Thread等の利用側がTryPopしてDispatchする。
HandlerはDispatch中だけ借用し、その呼び出しThreadで実行する。Queue Lock内にCallbackはない。
Handler例外は利用側へ伝播し、自動再実行しない。Scene / GPUへの反映はHandler側の責務。

QueueはFIFO、既定64件・受信MessagePack長合計64 KiB。実際のメモリには型・deque等の管理領域も加わる。
Limitsは生成時に固定し、0容量は投入不可。上限超過時は待機・無制限蓄積をせずSessionを失敗させる。
Policyは生成時のAllowPing / AllowSetReadyで制限する。受信時とDispatch時の双方で適用できる。
これは型の許可であり、利用者認証・接続先の権限判定ではない。

不正Frame、Policy違反、確保失敗、容量超過はFailedとし、Pendingと未処理Queueを全破棄する。
同じReceive呼び出し内で先行した正常Frameも、未取り出しなら破棄する。
エラーはstickyで、その後の受信は拒否する。診断は構文位置と型変換のフィールド経路を分離する。
Disconnectは未処理データを破棄し、既存の失敗原因を維持する。繰り返し通知できる。
FinishStreamはEOF通知で、未完Prefix/BodyがあればTruncated、なければClosed。どちらもQueueを破棄する。
EOFより先に配送したい正常値は、利用側が先にTryPopする必要がある。

TryPop済みの値は利用側が所有し、Disconnectで取り消されない。
再接続は新しいReceiverを生成し、旧Sessionとの対応・取り出し後の取消は利用側で管理する。
破棄前にProducer/Consumerを停止・joinすること。破棄と同時のメソッド呼び出しは許可しない。
1接続につきReceive Producerは1つとし、呼び出し順をWire順に保つ。
複数ConsumerのCallback実行順までFIFOにする機構はない。

## 検証と残課題

`scripts/test-windows-structured-data.ps1`でData回帰とNetwork CPUテストを実行する。
全分割位置・1 byte受信・結合Frame・切断・不正Envelope・容量超過・Workerから呼び出し側Threadへの
FIFO Dispatchを対象とする。既存429件は実Socketを使わないメモリ内Loopbackである。

2026-11-14の結果: MSVC CPUテストは既存5,643＋Network 429＝6,072チェック成功、警告0。
アプリDebug x64の増分ビルドも`RavenGenerateLinkDebugInformation=false`で成功。
既存CRT競合のLNK4098が1件。通常PDB生成はPhase 10で完了しなかった環境のため今回再試行していない。
GUIと外部ネットワークでの動作は検証していない。下記の追加検証はlocalhostの実Socketを使う。

## Windows TCP Loopback Transport

`Network/Windows/WindowsTcpSocket`は127.0.0.1へのBind / Connectに限定する。外部Interface、DNS、UDP、
IPv6、TLS、汎用接続Managerは追加しない。公開HeaderはWinsock型を露出せず、Windows以外ではこのAPIを定義しない。
Windowsテストとアプリのリンクに`ws2_32.lib`を追加する。

- ListenLoopback(0)はOSがPortを選択する。固定Portを試験環境へ強制しない。
- Socketはmove-onlyで、独自のWSAStartup参照を持つ。AcceptしたSocketも独立して所有し、
  Listener破棄後も生存する。CloseはSocket→WSACleanupの順で解放し、再通知できる。
- 全Socketを非Blockingにし、Connect / Accept / Send / Receiveの待機はsteady_clockの絶対期限で制限する。
  部分I/Oごとに同じ期限を再検査する。期限はOSのScheduling遅延までを抑えるリアルタイム保証ではない。
- Connectはselectのwritefds / exceptfdsとSO_ERRORで完了を判定する。失敗した仮Socketは破棄し、
  既存の接続・Accept出力を置換しない。SO_EXCLUSIVEADDRUSEでListenerの同一Address再利用を拒否する。
- SendAllは64 KiB以下の各Sendで返ったByte数だけ進める。I/O失敗・期限切れはSocketを閉じ、
  部分Frameを同じ接続へ最初から再送しない。成功はOSによる受付で、相手の処理完了ではない。
- Raw Receiveの期限切れはcount=0でSocketを保持し、再試行可能。引数不正も既存Socketを保持する。
  EOFはcount=0・成功でSocketを閉じる。他のI/O失敗はNative Codeを保存して閉じる。
  Peerの送信Half-closeをEOFとして接続全体を閉じる設計であり、EOF後の返信は対応しない。
- ShutdownSendは送信を終了する。PeerからのEOFまでは受信可能で、Socket解放はCloseが担当する。
  SO_LINGERを無効にし、Close自体では送信完了を待たない。配送完了が必要なら上位Protocolで応答を待つ。

`ReceiveControlStream`は固定4 KiBの受信BufferをFramerへ渡す。成功しても部分Frameの場合がある。
EOFはFinishStreamへ渡し、未完FrameはProtocol / Truncatedとなる。
Socketエラー、Receiverの拒否、不正Frame、制御接続の期限切れではSocketをCloseし、未処理Queueを破棄する。
Protocolエラー時はReceiverの既存の失敗原因を維持する。TimeoutはTcpError側に保存する。
正常値をDispatchする前に次の受信でEOFを処理するとQueueは消えるため、利用側は先にTryPopする。

Socketには内部Threadも並行Close用Lockもない。各Socketの操作は1つの所有Threadに限定する。
停止時は有限期限でI/Oを終え、WorkerをjoinしてからClose/Receiver破棄する。
別Threadで同じSocketをCloseして待機を解除する使い方や、送信と受信の同時操作はサポートしない。
TransportだけのSend失敗・明示CloseではReceiverを自動変更しない。関連ReceiverのDisconnectは呼び出し側で行う。
受信側のClose・Queue破棄をまとめる場合はReceiveControlStreamを使用する。

Microsoft API契約の参照:
[select](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select)、
[connect](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-connect)、
[closesocket](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-closesocket)。

実TCP検証は型付き双方向通信、分割Prefix/Body、結合Frame、代表的な途中EOF、不正長・不正MessagePack・
Version不一致、受信Policy/Queue超過、待機期限、Move、Listenerから独立した寿命、1 MiBのByte列一致、
Worker受信→join→呼び出し側ThreadでのFIFO Dispatchを対象とする。
OSによる短いSendやSend待機の発生回数は計測していない。低レベルReceiveの小容量読みによる部分I/Oと、
複数SendにまたがるByte列一致を確認する。WSAEWOULDBLOCK後のSend待機を強制する試験は未実施。
SandboxではConnectがWSAEACCES（10013）で拒否されたため、実Socket試験はsandbox外で実行する。
接続失敗をSkipして成功扱いにはしない。外部Interfaceで待ち受けることはない。

2026-11-14追加検証: 既存6,072＋Windows TCP 53＝6,125チェック成功、MSVC警告0。
アプリDebug x64もPDB生成無効で成功。初回の既存LNK4098が1件、最終増分ビルドは警告0。

高頻度同期は未導入。現段階で圧縮・Quantization・Deltaを追加せず、実用途・通信層が決まった段階で
MessagePackのByte数・Encode/Decode時間・帯域を計測し、必要な場合だけ別Codecを検討する。
UDP / 外部接続 / 他OS、認証・暗号化・再送、複数接続管理、実運用Lobby / RPC、性能閾値、Sanitizer / ThreadSanitizer、
確保失敗注入は別段階。接続数全体のメモリ予算・Rate Limitも利用側で追加が必要。
