#include "modbus_codec.h"

namespace {

void appendBigEndian16(QByteArray &data, uint16_t value) {
    data.append(static_cast<char>((value >> 8) & 0xff));
    data.append(static_cast<char>(value & 0xff));
}

uint8_t byteAt(const QByteArray &data, int index) {
    return static_cast<uint8_t>(static_cast<unsigned char>(data.at(index)));
}

uint16_t readBigEndian16(const QByteArray &data, int index) {
    return static_cast<uint16_t>((static_cast<uint16_t>(byteAt(data, index)) << 8)
                                 | byteAt(data, index + 1));
}

// LSB-first: coil N lives at bit (N % 8) of byte (N / 8) -- verified directly against a
// real pymodbus round-trip (spec.md Context), not assumed.
QVector<bool> unpackBits(const QByteArray &data, int offset, int byteCount) {
    QVector<bool> bits;
    bits.reserve(byteCount * 8);
    for (int i = 0; i < byteCount; ++i) {
        const uint8_t b = byteAt(data, offset + i);
        for (int bit = 0; bit < 8; ++bit) {
            bits.append((b & (1 << bit)) != 0);
        }
    }
    return bits;
}

QByteArray packBits(const QVector<bool> &values) {
    const int byteCount = (values.size() + 7) / 8;
    QByteArray packed(byteCount, '\0');
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i)) {
            packed[i / 8] = static_cast<char>(
                static_cast<uint8_t>(packed[i / 8]) | (1 << (i % 8)));
        }
    }
    return packed;
}

}  // namespace

namespace ModbusCodec {

QByteArray encodeReadHoldingRegistersRequest(const ReadHoldingRegistersRequest &request) {
    QByteArray data;
    data.reserve(12);

    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, 0x0006);
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x03));
    appendBigEndian16(data, request.startAddress);
    appendBigEndian16(data, request.quantity);

    return data;
}

QByteArray encodeWriteSingleRegisterRequest(const WriteSingleRegisterRequest &request) {
    QByteArray data;
    data.reserve(12);

    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, 0x0006);
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x06));
    appendBigEndian16(data, request.address);
    appendBigEndian16(data, request.value);

    return data;
}

ReadHoldingRegistersResponse decodeReadHoldingRegistersResponse(const QByteArray &data) {
    ReadHoldingRegistersResponse response;

    if (data.size() < 8) {
        return response;
    }

    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }

    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }

    if (functionCode != 0x03 || mbapLength < 3) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    const uint8_t byteCount = byteAt(data, 8);
    if ((byteCount % 2) != 0 || mbapLength != static_cast<uint16_t>(3 + byteCount)) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    response.registers.reserve(byteCount / 2);
    for (int index = 9; index < 9 + byteCount; index += 2) {
        response.registers.append(readBigEndian16(data, index));
    }
    response.status = ResponseStatus::Ok;
    return response;
}

WriteSingleRegisterResponse decodeWriteSingleRegisterResponse(const QByteArray &data) {
    WriteSingleRegisterResponse response;

    if (data.size() < 8) {
        return response;
    }

    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }

    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }

    if (functionCode != 0x06 || mbapLength != 6) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    response.address = readBigEndian16(data, 8);
    response.value = readBigEndian16(data, 10);
    response.status = ResponseStatus::Ok;
    return response;
}

QByteArray encodeWriteMultipleRegistersRequest(const WriteMultipleRegistersRequest &request) {
    QByteArray data;
    const int byteCount = request.values.size() * 2;
    data.reserve(13 + byteCount);

    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, static_cast<uint16_t>(7 + byteCount));
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x10));
    appendBigEndian16(data, request.startAddress);
    appendBigEndian16(data, static_cast<uint16_t>(request.values.size()));
    data.append(static_cast<char>(byteCount));
    for (uint16_t value : request.values) {
        appendBigEndian16(data, value);
    }

    return data;
}

WriteMultipleRegistersResponse decodeWriteMultipleRegistersResponse(const QByteArray &data) {
    WriteMultipleRegistersResponse response;

    if (data.size() < 8) {
        return response;
    }

    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }

    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }

    if (functionCode != 0x10 || mbapLength != 6) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    response.startAddress = readBigEndian16(data, 8);
    response.quantity = readBigEndian16(data, 10);
    response.status = ResponseStatus::Ok;
    return response;
}

QByteArray encodeReadInputRegistersRequest(const ReadInputRegistersRequest &request) {
    QByteArray data;
    data.reserve(12);

    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, 0x0006);
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x04));
    appendBigEndian16(data, request.startAddress);
    appendBigEndian16(data, request.quantity);

    return data;
}

ReadInputRegistersResponse decodeReadInputRegistersResponse(const QByteArray &data) {
    ReadInputRegistersResponse response;

    if (data.size() < 8) {
        return response;
    }

    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }

    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }

    if (functionCode != 0x04 || mbapLength < 3) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    const uint8_t byteCount = byteAt(data, 8);
    if ((byteCount % 2) != 0 || mbapLength != static_cast<uint16_t>(3 + byteCount)) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    response.registers.reserve(byteCount / 2);
    for (int index = 9; index < 9 + byteCount; index += 2) {
        response.registers.append(readBigEndian16(data, index));
    }
    response.status = ResponseStatus::Ok;
    return response;
}

QByteArray encodeReadCoilsRequest(const ReadCoilsRequest &request) {
    QByteArray data;
    data.reserve(12);
    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, 0x0006);
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x01));
    appendBigEndian16(data, request.startAddress);
    appendBigEndian16(data, request.quantity);
    return data;
}

ReadCoilsResponse decodeReadCoilsResponse(const QByteArray &data) {
    ReadCoilsResponse response;

    if (data.size() < 8) {
        return response;
    }
    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }
    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }
    if (functionCode != 0x01 || mbapLength < 3) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    const uint8_t byteCount = byteAt(data, 8);
    if (mbapLength != static_cast<uint16_t>(3 + byteCount)) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    response.coils = unpackBits(data, 9, byteCount);
    response.status = ResponseStatus::Ok;
    return response;
}

QByteArray encodeReadDiscreteInputsRequest(const ReadDiscreteInputsRequest &request) {
    QByteArray data;
    data.reserve(12);
    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, 0x0006);
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x02));
    appendBigEndian16(data, request.startAddress);
    appendBigEndian16(data, request.quantity);
    return data;
}

ReadDiscreteInputsResponse decodeReadDiscreteInputsResponse(const QByteArray &data) {
    ReadDiscreteInputsResponse response;

    if (data.size() < 8) {
        return response;
    }
    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }
    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }
    if (functionCode != 0x02 || mbapLength < 3) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    const uint8_t byteCount = byteAt(data, 8);
    if (mbapLength != static_cast<uint16_t>(3 + byteCount)) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    response.inputs = unpackBits(data, 9, byteCount);
    response.status = ResponseStatus::Ok;
    return response;
}

QByteArray encodeWriteSingleCoilRequest(const WriteSingleCoilRequest &request) {
    QByteArray data;
    data.reserve(12);
    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, 0x0006);
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x05));
    appendBigEndian16(data, request.address);
    appendBigEndian16(data, request.value ? 0xFF00 : 0x0000);
    return data;
}

WriteSingleCoilResponse decodeWriteSingleCoilResponse(const QByteArray &data) {
    WriteSingleCoilResponse response;

    if (data.size() < 8) {
        return response;
    }
    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }
    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }
    if (functionCode != 0x05 || mbapLength != 6) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    response.address = readBigEndian16(data, 8);
    response.value = readBigEndian16(data, 10) == 0xFF00;
    response.status = ResponseStatus::Ok;
    return response;
}

QByteArray encodeWriteMultipleCoilsRequest(const WriteMultipleCoilsRequest &request) {
    QByteArray data;
    const QByteArray packedBits = packBits(request.values);
    data.reserve(13 + packedBits.size());
    appendBigEndian16(data, request.transactionId);
    appendBigEndian16(data, 0x0000);
    appendBigEndian16(data, static_cast<uint16_t>(7 + packedBits.size()));
    data.append(static_cast<char>(request.unitId));
    data.append(static_cast<char>(0x0f));
    appendBigEndian16(data, request.startAddress);
    appendBigEndian16(data, static_cast<uint16_t>(request.values.size()));
    data.append(static_cast<char>(packedBits.size()));
    data.append(packedBits);
    return data;
}

WriteMultipleCoilsResponse decodeWriteMultipleCoilsResponse(const QByteArray &data) {
    WriteMultipleCoilsResponse response;

    if (data.size() < 8) {
        return response;
    }
    response.transactionId = readBigEndian16(data, 0);
    const uint16_t mbapLength = readBigEndian16(data, 4);
    const int frameSize = 6 + static_cast<int>(mbapLength);
    if (data.size() < frameSize) {
        return response;
    }
    if (readBigEndian16(data, 2) != 0x0000 || mbapLength < 2) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    const uint8_t functionCode = byteAt(data, 7);
    if ((functionCode & 0x80) != 0) {
        if (mbapLength < 3) {
            response.status = ResponseStatus::Malformed;
            return response;
        }
        response.status = ResponseStatus::Exception;
        response.exceptionCode = byteAt(data, 8);
        return response;
    }
    if (functionCode != 0x0f || mbapLength != 6) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    response.startAddress = readBigEndian16(data, 8);
    response.quantity = readBigEndian16(data, 10);
    response.status = ResponseStatus::Ok;
    return response;
}

}  // namespace ModbusCodec
