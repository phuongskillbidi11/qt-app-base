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

}  // namespace ModbusCodec
