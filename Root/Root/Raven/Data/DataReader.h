#pragma once

#include "Raven/Data/DataElement.h"

namespace Raven
{
namespace Data
{
class DataWriter;

// 不変Documentの非所有Reader。JSON固有の表記を持ち出さず、型とDecoded値を渡します。
// Document / Elementの寿命条件はDataElementと同じです。部分木をRootとしても利用できます。
class DataReader
{
public:
    explicit DataReader(DataElement root);
    DataElement GetRoot() const;
    // 反復走査でWriterへ転送します。Writerが失敗した時点で停止し、再試行はしません。
    // 失敗時はWriterが部分状態を持ち得ます。Reader自体とDocumentは変更しません。
    // error.Offsetは失敗した値・キーのSource位置。無効Root・走査用確保失敗では0です。
    bool Read(DataWriter& writer, DataError* error = nullptr) const;

private:
    DataElement m_Root;
};
}
}
