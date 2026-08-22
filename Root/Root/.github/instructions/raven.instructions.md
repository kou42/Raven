---
description: Describe when these instructions should be loaded by the agent based on task context
# applyTo: 'Describe when these instructions should be loaded by the agent based on task context' # when provided, instructions will automatically be added to the request context when the pattern matches an attached file
---

<!-- Tip: Use /create-instructions in chat to generate content with agent assistance -->

# GitHub Copilot Instructions

## 言語
- 回答は日本語で行ってください。
- コード内のコメントは必ず日本語で記述してください。

## C++ コーディング規約
- コメントアウトは必ず日本語で行ってください。
- if文内の処理は必ずブロック `{}` で囲むこと。
- !演算子はできるだけ使用せずに, == false のように明示的に比較すること。  
- nullチェックなどは、`if (ptr == nullptr)` のように明示的に行うこと。
- 改行は、コードの可読性を高めるために適切に使用すること。
- 実装時には重要な箇所ほどコメントアウトでの解説を丁寧に記載すること。
- 基本的にはコメントは消さなくても大丈夫です。更新する際は旧実装がコメントの内容と相違する場合などです。
- チャット内でも重要な箇所ほど解説を丁寧に記載すること。
- その後のおすすめのロードマップなどがあれば紹介すること。
- Githubのコミットのコメントなども日本語で記載するようにお願いします。

Provide project context and coding guidelines that AI should follow when generating code, answering questions, or reviewing changes.