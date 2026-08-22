#include "c3_codec.h"

#include <QRegularExpression>
#include <QVariant>

namespace {

void appendByte(QByteArray &data, uint8_t value) {
    data.append(static_cast<char>(value));
}

void appendLittleEndian16(QByteArray &data, uint16_t value) {
    appendByte(data, static_cast<uint8_t>(value & 0xFF));
    appendByte(data, static_cast<uint8_t>((value >> 8) & 0xFF));
}

uint8_t byteAt(const QByteArray &data, int index) {
    return static_cast<uint8_t>(data.at(index));
}

QByteArray encodeFrame(uint8_t command, uint16_t sessionId, int32_t requestNr,
                       const QByteArray &payload, bool includeSessionBlock = true) {
    QByteArray frameBody;
    appendByte(frameBody, C3Codec::kProtocolVersion);
    appendByte(frameBody, command);
    appendLittleEndian16(frameBody, static_cast<uint16_t>(
        (includeSessionBlock ? 4 : 0) + payload.size()));
    if (includeSessionBlock) {
        appendLittleEndian16(frameBody, sessionId);
        appendLittleEndian16(frameBody, static_cast<uint16_t>(requestNr));
    }
    frameBody.append(payload);

    const uint16_t checksum = C3Codec::crc16(frameBody);
    QByteArray frame;
    appendByte(frame, C3Codec::kFrameStart);
    frame.append(frameBody);
    appendLittleEndian16(frame, checksum);
    appendByte(frame, C3Codec::kFrameEnd);
    return frame;
}

using C3Codec::ResponseStatus;

struct DecodedFrame {
    ResponseStatus status = ResponseStatus::Incomplete;
    QByteArray payload;   // meaningful only when status == Ok
    uint8_t command = 0;  // the raw reply command byte; meaningful only when status == Ok
};

DecodedFrame decodeFrameGeneric(const QByteArray &data, bool stripSessionBlock) {
    DecodedFrame result;
    if (data.size() < 5) return result;
    const uint16_t length = static_cast<uint16_t>(
        byteAt(data, 3) | (static_cast<uint16_t>(byteAt(data, 4)) << 8));
    const int frameSize = 5 + static_cast<int>(length) + 2 + 1;
    if (data.size() < frameSize) return result;
    const int crcOffset = 5 + static_cast<int>(length);
    const uint16_t expectedCrc = static_cast<uint16_t>(
        byteAt(data, crcOffset) |
        (static_cast<uint16_t>(byteAt(data, crcOffset + 1)) << 8));
    const uint8_t command = byteAt(data, 2);
    if (byteAt(data, 0) != C3Codec::kFrameStart ||
        byteAt(data, frameSize - 1) != C3Codec::kFrameEnd ||
        C3Codec::crc16(data.mid(1, 4 + static_cast<int>(length))) != expectedCrc) {
        result.status = ResponseStatus::Malformed;
        return result;
    }
    if (command == C3Codec::kReplyError) {
        result.status = ResponseStatus::Rejected;
        return result;
    }
    QByteArray payload = data.mid(5, length);
    if (stripSessionBlock && payload.size() >= 4) {
        payload = payload.mid(4);
    }
    result.status = ResponseStatus::Ok;
    result.payload = payload;
    result.command = command;
    return result;
}

}  // namespace

namespace C3Codec {

uint16_t crc16(const QByteArray &data) {
    uint16_t crc = 0x0000;
    for (char rawByte : data) {
        crc ^= static_cast<uint8_t>(rawByte);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001U);
            } else {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

QByteArray encodeConnectSessionRequest(const ConnectSessionRequest &request) {
    return encodeFrame(kCommandConnectSession, kPreSessionId, kPreSessionRequestNr,
                       request.password.toLatin1());
}

ConnectSessionResponse decodeConnectSessionResponse(const QByteArray &data) {
    ConnectSessionResponse response;
    if (data.size() < 5) {
        return response;
    }

    const uint16_t length = static_cast<uint16_t>(
        byteAt(data, 3) | (static_cast<uint16_t>(byteAt(data, 4)) << 8));
    const int frameSize = 5 + static_cast<int>(length) + 2 + 1;
    if (data.size() < frameSize) {
        return response;
    }

    const int crcOffset = 5 + static_cast<int>(length);
    const uint16_t expectedCrc = static_cast<uint16_t>(
        byteAt(data, crcOffset) |
        (static_cast<uint16_t>(byteAt(data, crcOffset + 1)) << 8));
    const uint8_t command = byteAt(data, 2);
    if (byteAt(data, 0) != kFrameStart || byteAt(data, frameSize - 1) != kFrameEnd ||
        crc16(data.mid(1, 4 + static_cast<int>(length))) != expectedCrc ||
        (command != kReplyOk && command != kReplyError)) {
        response.status = ResponseStatus::Malformed;
        return response;
    }

    if (command == kReplyError) {
        response.status = ResponseStatus::Rejected;
        return response;
    }

    response.status = ResponseStatus::Ok;
    response.sessionId = static_cast<uint16_t>(
        byteAt(data, 5) | (static_cast<uint16_t>(byteAt(data, 6)) << 8));
    return response;
}

QByteArray encodeDisconnectRequest(const DisconnectRequest &request) {
    return encodeFrame(kCommandDisconnect, request.sessionId, request.requestNr, {});
}

QByteArray encodeConnectSessionLessRequest(const ConnectSessionLessRequest &request) {
    return encodeFrame(kCommandConnectSessionLess, 0, 0, request.password.toLatin1(), false);
}

QByteArray encodeSessionLessDisconnectRequest() {
    return encodeFrame(kCommandDisconnect, 0, 0, {}, false);
}

GenericReply decodeGenericReply(const QByteArray &data) {
    GenericReply reply;
    if (data.size() < 5) {
        return reply;
    }

    const uint16_t length = static_cast<uint16_t>(
        byteAt(data, 3) | (static_cast<uint16_t>(byteAt(data, 4)) << 8));
    const int frameSize = 5 + static_cast<int>(length) + 2 + 1;
    if (data.size() < frameSize) {
        return reply;
    }

    const int crcOffset = 5 + static_cast<int>(length);
    const uint16_t expectedCrc = static_cast<uint16_t>(
        byteAt(data, crcOffset) |
        (static_cast<uint16_t>(byteAt(data, crcOffset + 1)) << 8));
    const uint8_t command = byteAt(data, 2);
    if (byteAt(data, 0) != kFrameStart || byteAt(data, frameSize - 1) != kFrameEnd ||
        crc16(data.mid(1, 4 + static_cast<int>(length))) != expectedCrc ||
        (command != kReplyOk && command != kReplyError)) {
        reply.status = ResponseStatus::Malformed;
        return reply;
    }

    reply.status = command == kReplyError ? ResponseStatus::Rejected : ResponseStatus::Ok;
    return reply;
}

QVector<QPair<QString, QString>> parseKeyValuePairs(const QByteArray &line) {
    QVector<QPair<QString, QString>> pairs;
    const QRegularExpression pattern(QStringLiteral(R"(([\w~]+)=([^,\t]+))"));
    QRegularExpressionMatchIterator matches = pattern.globalMatch(QString::fromLatin1(line));
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        pairs.append(qMakePair(match.captured(1), match.captured(2)));
    }
    return pairs;
}

QByteArray encodeDataTableConfigRequest(bool includeSessionBlock, uint16_t sessionId,
                                        int32_t requestNr) {
    return encodeFrame(kCommandDataTableCfg, sessionId, requestNr, {}, includeSessionBlock);
}

DataTableConfigResponse decodeDataTableConfigResponse(const QByteArray &data,
                                                       bool includeSessionBlock) {
    DataTableConfigResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    response.status = frame.status;
    if (frame.status != ResponseStatus::Ok) {
        return response;
    }

    for (const QByteArray &line : frame.payload.split('\n')) {
        if (line.isEmpty()) {
            continue;
        }
        const QVector<QPair<QString, QString>> pairs = parseKeyValuePairs(line);
        if (pairs.isEmpty()) {
            continue;
        }

        DataTableConfig table;
        table.name = pairs.first().first;
        table.index = pairs.first().second.toInt();
        for (int pairIndex = 1; pairIndex < pairs.size(); ++pairIndex) {
            const QString &fieldDescriptor = pairs[pairIndex].second;
            if (fieldDescriptor.isEmpty()) {
                continue;
            }
            DataTableField field;
            field.name = pairs[pairIndex].first;
            field.type = fieldDescriptor.at(0).toLatin1();
            field.index = fieldDescriptor.mid(1).toInt();
            table.fields.append(field);
        }
        response.tables.append(table);
    }
    return response;
}

QByteArray encodeGetDataRequest(int tableIndex, const QVector<int> &fieldIndexes,
                                bool includeSessionBlock, uint16_t sessionId,
                                int32_t requestNr) {
    QByteArray payload;
    appendByte(payload, static_cast<uint8_t>(tableIndex));
    appendByte(payload, static_cast<uint8_t>(fieldIndexes.size()));
    for (int fieldIndex : fieldIndexes) {
        appendByte(payload, static_cast<uint8_t>(fieldIndex));
    }
    appendByte(payload, 0x00);
    appendByte(payload, 0x00);
    return encodeFrame(kCommandGetData, sessionId, requestNr, payload, includeSessionBlock);
}

GetDataResponse decodeGetDataResponse(const QByteArray &data, int expectedTableIndex,
                                      const QVector<DataTableField> &tableFields,
                                      bool includeSessionBlock) {
    GetDataResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    if (frame.status == ResponseStatus::Incomplete) {
        return response;
    }
    if (frame.status == ResponseStatus::Malformed) {
        response.status = GetDataStatus::Malformed;
        return response;
    }
    if (frame.status == ResponseStatus::Rejected) {
        response.status = GetDataStatus::Rejected;
        return response;
    }
    if (frame.command == kCommandPrepareData) {
        if (frame.payload.size() != 17) {
            response.status = GetDataStatus::Malformed;
            return response;
        }
        PrepareDataInfo info;
        info.compressed = byteAt(frame.payload, 0) != 0;
        info.dataLength = static_cast<uint32_t>(byteAt(frame.payload, 1)) |
            (static_cast<uint32_t>(byteAt(frame.payload, 2)) << 8) |
            (static_cast<uint32_t>(byteAt(frame.payload, 3)) << 16) |
            (static_cast<uint32_t>(byteAt(frame.payload, 4)) << 24);
        info.originalLength = static_cast<uint32_t>(byteAt(frame.payload, 5)) |
            (static_cast<uint32_t>(byteAt(frame.payload, 6)) << 8) |
            (static_cast<uint32_t>(byteAt(frame.payload, 7)) << 16) |
            (static_cast<uint32_t>(byteAt(frame.payload, 8)) << 24);
        info.checksum = static_cast<uint32_t>(byteAt(frame.payload, 9)) |
            (static_cast<uint32_t>(byteAt(frame.payload, 10)) << 8) |
            (static_cast<uint32_t>(byteAt(frame.payload, 11)) << 16) |
            (static_cast<uint32_t>(byteAt(frame.payload, 12)) << 24);
        info.packageLength = static_cast<uint32_t>(byteAt(frame.payload, 13)) |
            (static_cast<uint32_t>(byteAt(frame.payload, 14)) << 8) |
            (static_cast<uint32_t>(byteAt(frame.payload, 15)) << 16) |
            (static_cast<uint32_t>(byteAt(frame.payload, 16)) << 24);
        response.status = GetDataStatus::BigDataPending;
        response.prepareInfo = info;
        return response;
    }
    return parseGetDataPayload(frame.payload, expectedTableIndex, tableFields);
}

GetDataResponse parseGetDataPayload(const QByteArray &payload, int expectedTableIndex,
                                    const QVector<DataTableField> &tableFields) {
    GetDataResponse response;
    if (payload.size() < 2) {
        response.status = GetDataStatus::Malformed;
        return response;
    }

    if (byteAt(payload, 0) != static_cast<uint8_t>(expectedTableIndex)) {
        response.status = GetDataStatus::TableMismatch;
        return response;
    }
    const int fieldCount = byteAt(payload, 1);
    if (payload.size() < 2 + fieldCount) {
        response.status = GetDataStatus::Malformed;
        return response;
    }

    QVector<const DataTableField *> returnedFields;
    returnedFields.reserve(fieldCount);
    for (int fieldOffset = 0; fieldOffset < fieldCount; ++fieldOffset) {
        const int returnedIndex = byteAt(payload, 2 + fieldOffset);
        const DataTableField *matchingField = nullptr;
        for (const DataTableField &field : tableFields) {
            if (field.index == returnedIndex) {
                matchingField = &field;
                break;
            }
        }
        if (!matchingField) {
            response.status = GetDataStatus::Malformed;
            return response;
        }
        returnedFields.append(matchingField);
    }

    int offset = 2 + fieldCount;
    if (returnedFields.isEmpty() && offset != payload.size()) {
        response.status = GetDataStatus::Malformed;
        return response;
    }
    while (offset < payload.size()) {
        QVariantMap record;
        for (const DataTableField *field : returnedFields) {
            if (offset >= payload.size()) {
                response.status = GetDataStatus::Malformed;
                response.records.clear();
                return response;
            }
            const int valueSize = byteAt(payload, offset++);
            if (valueSize > payload.size() - offset) {
                response.status = GetDataStatus::Malformed;
                response.records.clear();
                return response;
            }

            if (field->type == 'i' || field->type == 'L') {
                if (valueSize > static_cast<int>(sizeof(qint64))) {
                    response.status = GetDataStatus::Malformed;
                    response.records.clear();
                    return response;
                }
                qint64 value = 0;
                for (int byteIndex = 0; byteIndex < valueSize; ++byteIndex) {
                    value |= static_cast<qint64>(byteAt(payload, offset + byteIndex))
                             << (8 * byteIndex);
                }
                record.insert(field->name, QVariant::fromValue<qlonglong>(value));
            } else if (field->type == 's') {
                record.insert(field->name,
                              QString::fromLatin1(payload.constData() + offset, valueSize));
            } else {
                response.status = GetDataStatus::Malformed;
                response.records.clear();
                return response;
            }
            offset += valueSize;
        }
        response.records.append(record);
    }

    response.status = GetDataStatus::Ok;
    return response;
}

QByteArray encodeTransmitDataRequest(uint32_t offset, bool includeSessionBlock,
                                     uint16_t sessionId, int32_t requestNr) {
    QByteArray payload(4, '\0');
    payload[0] = static_cast<char>(offset & 0xFF);
    payload[1] = static_cast<char>((offset >> 8) & 0xFF);
    payload[2] = static_cast<char>((offset >> 16) & 0xFF);
    payload[3] = static_cast<char>((offset >> 24) & 0xFF);
    return encodeFrame(kCommandTransmitData, sessionId, requestNr, payload, includeSessionBlock);
}

TransmitDataResponse decodeTransmitDataResponse(const QByteArray &data, bool includeSessionBlock) {
    TransmitDataResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    response.status = frame.status;
    if (frame.status != ResponseStatus::Ok) {
        return response;
    }
    if (frame.payload.size() < 4) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    response.offset = static_cast<uint32_t>(byteAt(frame.payload, 0)) |
        (static_cast<uint32_t>(byteAt(frame.payload, 1)) << 8) |
        (static_cast<uint32_t>(byteAt(frame.payload, 2)) << 16) |
        (static_cast<uint32_t>(byteAt(frame.payload, 3)) << 24);
    response.chunkData = frame.payload.mid(4);
    return response;
}

QByteArray encodeFreeDataRequest(bool includeSessionBlock, uint16_t sessionId, int32_t requestNr) {
    return encodeFrame(kCommandFreeData, sessionId, requestNr, {}, includeSessionBlock);
}

QByteArray encodeGetDataCountRequest(int tableIndex, bool includeSessionBlock,
                                     uint16_t sessionId, int32_t requestNr) {
    const QByteArray payload(1, static_cast<char>(tableIndex));
    return encodeFrame(kCommandGetDataCount, sessionId, requestNr, payload, includeSessionBlock);
}

GetDataCountResponse decodeGetDataCountResponse(const QByteArray &data, bool includeSessionBlock) {
    GetDataCountResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    if (frame.status == ResponseStatus::Ok && frame.payload.size() != 4) {
        response.status = ResponseStatus::Malformed;
        return response;
    }
    response.status = frame.status;
    if (frame.status != ResponseStatus::Ok) {
        return response;
    }
    response.count = static_cast<uint32_t>(
        static_cast<uint8_t>(frame.payload[0])) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.payload[1])) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.payload[2])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(frame.payload[3])) << 24);
    return response;
}

QByteArray encodeRtlogBinaryRequest(bool includeSessionBlock, uint16_t sessionId,
                                    int32_t requestNr) {
    return encodeFrame(kCommandRtlogBinary, sessionId, requestNr, {}, includeSessionBlock);
}

QByteArray encodeRtlogKeyValueRequest(bool includeSessionBlock, uint16_t sessionId,
                                      int32_t requestNr) {
    return encodeFrame(kCommandRtlogKeyValue, sessionId, requestNr, {}, includeSessionBlock);
}

RtLogBinaryResponse decodeRtlogBinaryResponse(const QByteArray &data,
                                               bool includeSessionBlock) {
    RtLogBinaryResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    if (frame.status == ResponseStatus::Incomplete) {
        return response;
    }
    if (frame.status == ResponseStatus::Malformed) {
        response.status = RtLogStatus::Malformed;
        return response;
    }
    if (frame.status == ResponseStatus::Rejected) {
        response.status = RtLogStatus::Rejected;
        return response;
    }
    if (frame.payload.size() % 16 != 0) {
        response.status = RtLogStatus::NotBinaryMode;
        return response;
    }

    for (int offset = 0; offset < frame.payload.size(); offset += 16) {
        const QByteArray rec = frame.payload.mid(offset, 16);
        RtLogRecord record;
        record.eventType = byteAt(rec, 10);
        if (record.eventType == 255) {
            record.kind = RtLogRecordKind::DoorAlarmStatus;
            record.alarmStatus = rec.mid(0, 4);
            record.dssStatus = rec.mid(4, 4);
            record.verified = byteAt(rec, 9);
        } else {
            record.kind = RtLogRecordKind::Event;
            record.cardNo = static_cast<uint32_t>(byteAt(rec, 0)) |
                            (static_cast<uint32_t>(byteAt(rec, 1)) << 8) |
                            (static_cast<uint32_t>(byteAt(rec, 2)) << 16) |
                            (static_cast<uint32_t>(byteAt(rec, 3)) << 24);
            record.pin = static_cast<uint32_t>(byteAt(rec, 4)) |
                         (static_cast<uint32_t>(byteAt(rec, 5)) << 8) |
                         (static_cast<uint32_t>(byteAt(rec, 6)) << 16) |
                         (static_cast<uint32_t>(byteAt(rec, 7)) << 24);
            record.verified = byteAt(rec, 8);
            record.doorId = byteAt(rec, 9);
            record.inOutState = byteAt(rec, 11);
        }
        record.timeSecond = static_cast<uint32_t>(byteAt(rec, 12)) |
                            (static_cast<uint32_t>(byteAt(rec, 13)) << 8) |
                            (static_cast<uint32_t>(byteAt(rec, 14)) << 16) |
                            (static_cast<uint32_t>(byteAt(rec, 15)) << 24);
        response.records.append(record);
    }
    response.status = RtLogStatus::Ok;
    return response;
}

RtLogKeyValueResponse decodeRtlogKeyValueResponse(const QByteArray &data,
                                                   bool includeSessionBlock) {
    RtLogKeyValueResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    response.status = frame.status;
    if (frame.status != ResponseStatus::Ok) {
        return response;
    }
    if (!frame.payload.isEmpty()) {
        const QVector<QPair<QString, QString>> pairs = parseKeyValuePairs(frame.payload);
        if (!pairs.isEmpty()) {
            QVariantMap record;
            for (const auto &pair : pairs) {
                record.insert(pair.first, pair.second);
            }
            response.records.append(record);
        }
    }
    return response;
}

QByteArray encodeControlRequest(ControlOperation operation, uint8_t param1, uint8_t param2,
                                uint8_t param3, bool includeSessionBlock, uint16_t sessionId,
                                int32_t requestNr) {
    QByteArray payload;
    payload.append(static_cast<char>(static_cast<uint8_t>(operation)));
    payload.append(static_cast<char>(param1));
    payload.append(static_cast<char>(param2));
    payload.append(static_cast<char>(param3));
    payload.append(char(0));   // param4, reserved
    return encodeFrame(kCommandControl, sessionId, requestNr, payload, includeSessionBlock);
}

QByteArray encodeGetParamRequest(const QStringList &parameterNames, bool includeSessionBlock,
                                 uint16_t sessionId, int32_t requestNr) {
    const QByteArray payload = parameterNames.join(QLatin1Char(',')).toLatin1();
    return encodeFrame(kCommandGetParam, sessionId, requestNr, payload, includeSessionBlock);
}

GetParamResponse decodeGetParamResponse(const QByteArray &data, bool includeSessionBlock) {
    GetParamResponse response;
    const DecodedFrame frame = decodeFrameGeneric(data, includeSessionBlock);
    response.status = frame.status;
    if (frame.status != ResponseStatus::Ok) {
        return response;
    }
    if (!frame.payload.isEmpty()) {
        const QVector<QPair<QString, QString>> pairs = parseKeyValuePairs(frame.payload);
        for (const auto &pair : pairs) {
            response.values.insert(pair.first, pair.second);
        }
    }
    return response;
}

QByteArray encodeSetParamRequest(const QVariantMap &values, bool includeSessionBlock,
                                 uint16_t sessionId, int32_t requestNr) {
    QStringList pairs;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        pairs << QStringLiteral("%1=%2").arg(it.key(), it.value().toString());
    }
    const QByteArray payload = pairs.join(QLatin1Char(',')).toLatin1();
    return encodeFrame(kCommandGetParam, sessionId, requestNr, payload, includeSessionBlock);
}

}  // namespace C3Codec
