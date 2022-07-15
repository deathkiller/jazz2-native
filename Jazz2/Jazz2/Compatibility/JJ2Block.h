#pragma once

#include "../../Common.h"
#include "../../nCine/IO/IFileStream.h"

using namespace nCine;

namespace Jazz2::Compatibility
{
    class JJ2Block
    {
    public:
        JJ2Block(std::unique_ptr<IFileStream> s, int length, int uncompressedLength = 0);

        void SeekTo(int offset);
        void DiscardBytes(int length);

        bool ReadBool();
        uint8_t ReadByte();
        int16_t ReadInt16();
        uint16_t ReadUInt16();
        int32_t ReadInt32();
        uint32_t ReadUInt32();
        int32_t ReadUint7bitEncoded();
        float ReadFloat();
        float ReadFloatEncoded();
        //byte[] ReadRawBytes(int length);


    private:
        std::unique_ptr<IFileStream> _stream;
        std::unique_ptr<byte[]> _buffer;
    };
}