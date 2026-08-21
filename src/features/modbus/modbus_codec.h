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

struct WriteSingleRegisterRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t address = 0;
    uint16_t value = 0;
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket.
QByteArray encodeWriteSingleRegisterRequest(const WriteSingleRegisterRequest &request);

struct WriteSingleRegisterResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;   // meaningful only when status == Exception
    uint16_t address = 0;        // meaningful only when status == Ok
    uint16_t value = 0;          // meaningful only when status == Ok
};

// Decodes exactly one Write Single Register response frame from the front of `data`. A
// successful write echoes the request's address and value back exactly, per the Modbus
// specification -- verified directly against a real pymodbus server, not assumed.
WriteSingleRegisterResponse decodeWriteSingleRegisterResponse(const QByteArray &data);

struct WriteMultipleRegistersRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t startAddress = 0;
    QVector<uint16_t> values;   // 1-123 registers per the Modbus specification
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket.
QByteArray encodeWriteMultipleRegistersRequest(const WriteMultipleRegistersRequest &request);

struct WriteMultipleRegistersResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;   // meaningful only when status == Exception
    uint16_t startAddress = 0;   // meaningful only when status == Ok
    uint16_t quantity = 0;       // meaningful only when status == Ok
};

// Decodes exactly one Write Multiple Registers response frame from the front of `data`. A
// successful write echoes the request's start address and register count back -- not the
// values themselves -- per the Modbus specification, verified directly against a real
// pymodbus client/server exchange, not assumed.
WriteMultipleRegistersResponse decodeWriteMultipleRegistersResponse(const QByteArray &data);

struct ReadInputRegistersRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t startAddress = 0;
    uint16_t quantity = 1;   // valid range 1-125 per the Modbus specification
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket.
QByteArray encodeReadInputRegistersRequest(const ReadInputRegistersRequest &request);

struct ReadInputRegistersResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;      // meaningful only when status == Exception
    QVector<uint16_t> registers;    // meaningful only when status == Ok
};

// Decodes exactly one Read Input Registers response frame from the front of `data`. Byte-
// identical shape to Read Holding Registers -- only the function code differs -- verified
// directly against a real pymodbus server, not assumed.
ReadInputRegistersResponse decodeReadInputRegistersResponse(const QByteArray &data);

struct ReadCoilsRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t startAddress = 0;
    uint16_t quantity = 1;   // valid range 1-2000 per the Modbus specification
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket.
QByteArray encodeReadCoilsRequest(const ReadCoilsRequest &request);

struct ReadCoilsResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;   // meaningful only when status == Exception
    QVector<bool> coils;         // meaningful only when status == Ok; LSB-first within each
                                 // byte (coil N = bit N of the byte at N/8) -- verified
                                 // directly against a real pymodbus round-trip, not assumed.
                                 // Includes any trailing padding bits in the last byte --
                                 // the caller, which knows the requested quantity, decides
                                 // how many are meaningful (same boundary this codec already
                                 // draws for register counts).
};

// Decodes exactly one Read Coils response frame from the front of `data`.
ReadCoilsResponse decodeReadCoilsResponse(const QByteArray &data);

struct ReadDiscreteInputsRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t startAddress = 0;
    uint16_t quantity = 1;   // valid range 1-2000 per the Modbus specification
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket.
QByteArray encodeReadDiscreteInputsRequest(const ReadDiscreteInputsRequest &request);

struct ReadDiscreteInputsResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;   // meaningful only when status == Exception
    QVector<bool> inputs;        // meaningful only when status == Ok; same LSB-first
                                 // packing as ReadCoilsResponse::coils.
};

// Decodes exactly one Read Discrete Inputs response frame from the front of `data`. Byte-
// identical shape to Read Coils -- only the function code differs.
ReadDiscreteInputsResponse decodeReadDiscreteInputsResponse(const QByteArray &data);

struct WriteSingleCoilRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t address = 0;
    bool value = false;
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket. Encodes
// `value` as 0xFF00 (ON) or 0x0000 (OFF) per the Modbus specification.
QByteArray encodeWriteSingleCoilRequest(const WriteSingleCoilRequest &request);

struct WriteSingleCoilResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;   // meaningful only when status == Exception
    uint16_t address = 0;        // meaningful only when status == Ok
    bool value = false;          // meaningful only when status == Ok
};

// Decodes exactly one Write Single Coil response frame from the front of `data`. A
// successful write echoes the request exactly, per the Modbus specification -- verified
// directly against a real pymodbus server, not assumed.
WriteSingleCoilResponse decodeWriteSingleCoilResponse(const QByteArray &data);

struct WriteMultipleCoilsRequest {
    uint16_t transactionId = 0;
    uint8_t unitId = 1;
    uint16_t startAddress = 0;
    QVector<bool> values;   // 1-1968 coils per the Modbus specification
};

// Builds the full MBAP header + PDU byte sequence ready to write to a socket, LSB-first
// bit-packed per the Modbus specification -- verified directly against a real pymodbus
// round-trip, not assumed.
QByteArray encodeWriteMultipleCoilsRequest(const WriteMultipleCoilsRequest &request);

struct WriteMultipleCoilsResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t transactionId = 0;
    uint8_t exceptionCode = 0;   // meaningful only when status == Exception
    uint16_t startAddress = 0;   // meaningful only when status == Ok
    uint16_t quantity = 0;       // meaningful only when status == Ok
};

// Decodes exactly one Write Multiple Coils response frame from the front of `data`. A
// successful write echoes the request's start address and coil count back -- not the
// values themselves -- per the Modbus specification, verified directly against a real
// pymodbus client/server exchange, not assumed.
WriteMultipleCoilsResponse decodeWriteMultipleCoilsResponse(const QByteArray &data);

}  // namespace ModbusCodec
