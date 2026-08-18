#pragma once

#include <QByteArray>
#include <QVector>

#include <cstdint>

// Modbus TCP framing (the MBAP header) plus exactly one function: Read Holding Registers
// (function code 0x03). Pure encode/decode -- no socket, no I/O. See spec.md D3 for why only
// one function code, and DECISION_LOG.md's 2026-08-18 entry for why this codec never needs
// to resolve multi-register byte-order ambiguity: it reports raw 16-bit register values
// exactly as the wire format defines them (unambiguous), and leaves interpreting anything
// wider than one register to whatever consumes those values later -- the same boundary
// mq_codec already draws around `payload`.

namespace ModbusCodec {

struct ReadHoldingRegistersRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t startAddress = 0;
    uint16_t quantity = 1;   // valid range 1-125 per the Modbus specification
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket.
QByteArray encodeReadHoldingRegistersRequest(const ReadHoldingRegistersRequest &request);

enum class ResponseStatus {
    Incomplete,   // fewer bytes than the MBAP header's own Length field promises -- wait for more
    Malformed,    // enough bytes, but the framing itself is inconsistent (wrong protocol id, etc.)
    Exception,    // a valid Modbus exception response (function code | 0x80)
    Ok
};

struct ReadHoldingRegistersResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;          // meaningful only when status == Exception
    QVector<uint16_t> registers;        // meaningful only when status == Ok
};

// Decodes exactly one response frame from the front of `data`. Does not consume/modify
// `data` -- the caller (ModbusConnection, not this codec) owns buffering partial reads.
ReadHoldingRegistersResponse decodeReadHoldingRegistersResponse(const QByteArray &data);

}  // namespace ModbusCodec
