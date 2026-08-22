#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <cstdint>

// C3/InBio access-control panel wire protocol (TCP port 4370). Frame shape and CRC
// parameters were learned by reading zkaccess-c3-py (GPL-3.0) as documentation of an
// otherwise-unpublished protocol; this is a from-scratch implementation of that shape, not
// a port of its code -- see spec.md D3 of .plans/2026-08-21-c3-protocol-session.
//
// Frame: 0xAA (start) + version(1) + command(1) + length_lsb(1) + length_msb(1)
//        + [session_id_lsb, session_id_msb, request_nr_lsb, request_nr_msb]  (4 bytes,
//          present on every message once a session exists)
//        + payload (`length` bytes total, including the 4 above when present)
//        + crc16_lsb + crc16_msb + 0x55 (end)
// CRC-16/ARC (poly 0xA001, init 0x0000, reflected) computed over version..payload -- not
// the start byte, not the CRC bytes themselves, not the end byte.

namespace C3Codec {

constexpr uint8_t kFrameStart = 0xAA;
constexpr uint8_t kFrameEnd = 0x55;
constexpr uint8_t kProtocolVersion = 0x01;
constexpr uint8_t kCommandConnectSessionLess = 0x01;
constexpr uint8_t kCommandConnectSession = 0x76;
constexpr uint8_t kCommandDisconnect = 0x02;
constexpr uint8_t kReplyOk = 0xC8;
constexpr uint8_t kReplyError = 0xC9;

// Sentinel values the panel accepts in the very first CONNECT_SESSION frame, before a real
// session exists -- an empirically-discovered protocol fact (confirmed against real device
// behavior documented by zkaccess-c3-py), not a stylistic choice. Do not change these.
constexpr uint16_t kPreSessionId = 0xFEFE;
constexpr int32_t kPreSessionRequestNr = -258;

// CRC-16/ARC: poly 0xA001, init 0x0000, bit-by-bit, reflected. Verify your implementation
// against crc16(QByteArray("123456789")) == 0xBB3D (the standard published CRC-16/ARC check
// value) before relying on any other test vector in this file.
uint16_t crc16(const QByteArray &data);

struct ConnectSessionRequest {
    QString password;   // empty = no-password connect
};
// Builds the full frame (0xAA .. 0x55) ready to write to a socket. Uses kPreSessionId /
// kPreSessionRequestNr as the session/request-number fields (session_id is truthy so the
// 4-byte session/request-nr block IS included, per the wire format above -- this happens
// even though no real session exists yet; that is the protocol's own behavior, not a bug).
// If `password` is non-empty, its ASCII bytes are appended as the payload after the
// session/request-nr block; if empty, no extra payload bytes are appended (payload = just
// the 4 session/request-nr bytes, so `length` = 4).
QByteArray encodeConnectSessionRequest(const ConnectSessionRequest &request);

enum class ResponseStatus {
    Incomplete,   // fewer bytes than the frame's own length field promises -- wait for more
    Malformed,    // wrong start/end marker, bad CRC, or command byte is neither kReplyOk nor kReplyError
    Rejected,     // well-formed frame, command byte == kReplyError
    Ok
};

struct ConnectSessionResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint16_t sessionId = 0;   // meaningful only when status == Ok; payload bytes 0-1, little-endian
};
// Decodes exactly one frame from the front of `data`. Does not consume/modify `data` -- the
// caller (C3Connection) owns buffering partial reads, matching ModbusCodec's convention.
ConnectSessionResponse decodeConnectSessionResponse(const QByteArray &data);

struct DisconnectRequest {
    uint16_t sessionId = 0;
    int32_t requestNr = 0;
};
// Builds the full frame. No corresponding decode function in this phase -- disconnect is
// fire-and-forget: send this frame, then close the socket. See spec.md's Goal ("cleanly
// DISCONNECT") -- confirming the panel's own disconnect reply is not required to satisfy it.
QByteArray encodeDisconnectRequest(const DisconnectRequest &request);

// CONNECT_SESSION_LESS omits the session/request-nr prefix entirely (unlike
// CONNECT_SESSION, which always includes it via kPreSessionId/kPreSessionRequestNr) --
// confirmed live against a real panel that rejects CONNECT_SESSION outright: see
// tests.md's F1 result for the exact captured bytes.
struct ConnectSessionLessRequest {
    QString password;   // empty = no-password connect
};
QByteArray encodeConnectSessionLessRequest(const ConnectSessionLessRequest &request);

// Builds a DISCONNECT frame with no session/request-nr prefix, for a session-less
// connection (there is no session ID to send). Distinct from encodeDisconnectRequest,
// which always includes the session/request-nr block for a real session.
QByteArray encodeSessionLessDisconnectRequest();

// A reply with no session-ID payload to interpret -- used for CONNECT_SESSION_LESS's own
// reply and any DISCONNECT reply, both confirmed live to carry a zero-length payload on
// success. Distinct from ConnectSessionResponse, which extracts a session ID from the
// first two payload bytes and would misread a zero-length payload.
struct GenericReply {
    ResponseStatus status = ResponseStatus::Incomplete;
};
GenericReply decodeGenericReply(const QByteArray &data);

constexpr uint8_t kCommandDataTableCfg = 0x06;
constexpr uint8_t kCommandGetData = 0x08;

// DATATABLE_CFG's reply is ASCII "key=value,key=value,..." text (one such line per table,
// lines separated by '\n') -- the first payload shape in this codec that is not raw binary.
// Matches the same comma-separated pattern zkaccess-c3-py documents for GETPARAM/CONTROL too
// (a future phase's own concern, not this one's) -- reusable as-is.
QVector<QPair<QString, QString>> parseKeyValuePairs(const QByteArray &line);

struct DataTableField {
    QString name;
    char type = 'i';   // 'i' = little-endian unsigned int, 's' = ASCII string, 'L' = little-endian unsigned int ("long"), decoded identically to 'i' -- found live on real hardware for wide fields like a full card number; no evidence its wire encoding differs from 'i'
    int index = 0;
};

struct DataTableConfig {
    QString name;
    int index = 0;
    QVector<DataTableField> fields;
};

// Reuses the existing ResponseStatus enum (Incomplete/Malformed/Rejected/Ok) -- do not add a
// second, near-identical enum for this.
struct DataTableConfigResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    QVector<DataTableConfig> tables;   // meaningful only when status == Ok
};

// `includeSessionBlock`/`sessionId`/`requestNr` control the same 4-byte session/request-nr
// prefix as the session-layer functions -- true for a real (non-session-less) connection,
// false for the session-less path this codebase's one confirmed real panel actually uses.
QByteArray encodeDataTableConfigRequest(bool includeSessionBlock, uint16_t sessionId,
                                        int32_t requestNr);
// Decodes exactly one frame from the front of `data`. `includeSessionBlock` must match
// whatever the connection actually negotiated -- if true, the 4-byte session/request-nr
// prefix is stripped from the payload before the kv-text is parsed (a reply carries that
// prefix on every message once a session exists, not only on the connect reply itself).
DataTableConfigResponse decodeDataTableConfigResponse(const QByteArray &data,
                                                       bool includeSessionBlock);

// GETDATA request payload = raw bytes: tableIndex, fieldCount, fieldIndex (one byte each, in
// `fieldIndexes`' given order), then two reserved zero bytes (purpose undocumented even in
// the reference project -- carried through unchanged, not invented).
QByteArray encodeGetDataRequest(int tableIndex, const QVector<int> &fieldIndexes,
                                bool includeSessionBlock, uint16_t sessionId,
                                int32_t requestNr);

constexpr uint8_t kCommandPrepareData = 0x0D;
constexpr uint8_t kCommandTransmitData = 0x0E;
constexpr uint8_t kCommandFreeData = 0x0F;

// CMD_PREPARE_DATA's reply payload is always exactly 17 bytes -- found by decompiling
// plcommpro.dll's real GetDeviceData/ZEMBPRO_READCMDDATA internals (interoperability
// reverse engineering of a closed vendor binary, the same discipline already used for
// GETDATACOUNT), live-confirmed against a real panel. Sent instead of a normal GETDATA
// reply whenever the actual payload is too large for one frame.
struct PrepareDataInfo {
    bool compressed = false;
    uint32_t dataLength = 0;
    uint32_t originalLength = 0;
    uint32_t checksum = 0;
    uint32_t packageLength = 0;
};

enum class GetDataStatus { Incomplete, Malformed, Rejected, TableMismatch, Ok, BigDataPending };

struct GetDataResponse {
    GetDataStatus status = GetDataStatus::Incomplete;
    QVector<QVariantMap> records;   // meaningful only when status == Ok
    PrepareDataInfo prepareInfo;    // meaningful only when status == BigDataPending
};
// `expectedTableIndex` must match the reply's own echoed table index or the result is
// TableMismatch. `tableFields` must be the DataTableConfig.fields for the table being
// queried -- used to map each returned field index to its name/type when decoding records.
// Decodes exactly one frame from the front of `data`; matches every other decode function's
// convention of not consuming/modifying `data`.
GetDataResponse decodeGetDataResponse(const QByteArray &data, int expectedTableIndex,
                                      const QVector<DataTableField> &tableFields,
                                      bool includeSessionBlock);

// NEW -- the record-parsing logic decodeGetDataResponse already had, extracted so it can
// also be called directly on a reassembled big-data buffer (no frame markers to strip --
// see spec.md D3 of .plans/2026-08-22-c3-getdata-bigdata). decodeGetDataResponse itself now
// calls this internally for its own non-big-data path; behavior for that path is unchanged.
GetDataResponse parseGetDataPayload(const QByteArray &payload, int expectedTableIndex,
                                    const QVector<DataTableField> &tableFields);

// CMD_TRANSMIT_DATA request payload is exactly 4 bytes: a little-endian uint32 byte offset
// into the pending big-data buffer, starting at 0.
QByteArray encodeTransmitDataRequest(uint32_t offset, bool includeSessionBlock,
                                     uint16_t sessionId, int32_t requestNr);

// Reply payload is [echoedOffset:4 bytes LE][chunk bytes...] -- echoedOffset must match what
// was requested; chunk length is the negotiated packageLength except possibly the final,
// shorter chunk.
struct TransmitDataResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint32_t offset = 0;
    QByteArray chunkData;   // meaningful only when status == Ok
};
TransmitDataResponse decodeTransmitDataResponse(const QByteArray &data, bool includeSessionBlock);

// CMD_FREE_DATA has an empty request payload; its reply is a bare ack/nack -- reuse the
// existing decodeGenericReply(data) as-is, no new response type.
QByteArray encodeFreeDataRequest(bool includeSessionBlock, uint16_t sessionId, int32_t requestNr);

constexpr uint8_t kCommandGetDataCount = 0x0A;

// Request payload is exactly one byte: the table's resolved binary index (from
// DATATABLE_CFG) -- confirmed live against 8 real tables on a real panel; see spec.md D2
// of .plans/2026-08-22-c3-getdatacount. NOT the table's ASCII name -- that was tried too and
// returns Ok with a zero-length payload instead of a count.
QByteArray encodeGetDataCountRequest(int tableIndex, bool includeSessionBlock,
                                     uint16_t sessionId, int32_t requestNr);

// Reply payload is always exactly 4 bytes: a little-endian uint32 record count -- confirmed
// live on 8 different real tables (counts from 0 into the thousands), never a different
// length on success.
struct GetDataCountResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    uint32_t count = 0;   // meaningful only when status == Ok
};
GetDataCountResponse decodeGetDataCountResponse(const QByteArray &data, bool includeSessionBlock);

// Decodes a raw C3DateTime-encoded seconds value (found in RTLOG Event/DoorAlarmStatus
// records' timeSecond field and every table's own "Time_second" field) into
// "YYYY-MM-DD HH:MM:SS". Pure integer arithmetic on a fixed calendar breakdown -- not a
// Unix timestamp, so no QDateTime/timezone involved. Confirmed against two independent real
// values: 347748895 -> "2010-10-26 20:54:55" (this file's own pre-existing worked example)
// and a live-captured 853507565 -> "2026-07-21 13:26:05", cross-checked against the
// ZKTecoProtocol reference app's own displayed value for that same live record.
QString decodeC3DateTime(uint32_t raw);

constexpr uint8_t kCommandGetParam = 0x04;

// Request payload is plain ASCII text: the requested parameter names, comma-separated, no
// other structure -- matches core.py's own get_device_param() exactly
// (",".join(request_parameters)).
QByteArray encodeGetParamRequest(const QStringList &parameterNames, bool includeSessionBlock,
                                 uint16_t sessionId, int32_t requestNr);

// Reply payload is the same "key=value,key=value,..." shape already implemented for
// DATATABLE_CFG and RTLOG_KEYVALUE -- reuses parseKeyValuePairs exactly as-is.
struct GetParamResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    QVariantMap values;   // parameter name -> string value; only what the panel returned,
                          // no assumption all requested names come back
};
GetParamResponse decodeGetParamResponse(const QByteArray &data, bool includeSessionBlock);

// SETPARAM reuses kCommandGetParam -- see spec.md D1 of .plans/2026-08-22-c3-setparam:
// GETPARAM's own request payload is bare comma-separated names ("get"); this request's
// payload is comma-separated Name=Value pairs ("set") -- same command byte, distinguished
// by the panel purely on payload shape. QVariant values are converted via .toString().
// Reply is a bare ack/nack -- decode with the existing decodeGenericReply(data), no new
// response type.
QByteArray encodeSetParamRequest(const QVariantMap &values, bool includeSessionBlock,
                                 uint16_t sessionId, int32_t requestNr);

constexpr uint8_t kCommandControl = 0x05;

// Values match consts.py's ControlOperation exactly -- confirmed against both
// controldevice.py's docstring and its actual code, which agree with each other here
// (unlike rtlog.py's own docstring/code discrepancy from the prior phase).
enum class ControlOperation : uint8_t {
    Output = 1,
    CancelAlarm = 2,
    RestartDevice = 3,
    EnableDisableNormalOpen = 4
};
enum class ControlOutputAddress : uint8_t { Door = 1, Aux = 2 };

// Builds the 5-byte CONTROL payload [operation, param1, param2, param3, 0 (param4,
// reserved)] and wraps it in a frame the same way every other command does.
// Operation-specific parameter meaning (see spec.md's own table):
//   Output: param1=door/aux number, param2=ControlOutputAddress, param3=duration
//           (0=close, 255=stay open/normal-open, 1-254=open N seconds then auto-close)
//   CancelAlarm / RestartDevice: all params 0
//   EnableDisableNormalOpen: param1=door number, param2=0(disable)/1(enable), param3=0
QByteArray encodeControlRequest(ControlOperation operation, uint8_t param1, uint8_t param2,
                                uint8_t param3, bool includeSessionBlock, uint16_t sessionId,
                                int32_t requestNr);

constexpr uint8_t kCommandRtlogBinary = 0x0B;
constexpr uint8_t kCommandRtlogKeyValue = 0x79;

enum class RtLogRecordKind { Event, DoorAlarmStatus };

// One 16-byte binary RT log record, tagged by kind. Both kinds share the same envelope;
// only bytes 0-11 are interpreted differently -- see decodeRtlogBinaryResponse's own
// implementation notes for the exact byte offsets, which follow zkaccess-c3-py's actual
// code, not its own (in places incorrect) docstring -- see spec.md D3.
struct RtLogRecord {
    RtLogRecordKind kind = RtLogRecordKind::Event;
    uint32_t cardNo = 0;       // Event only, little-endian, bytes 0-3
    uint32_t pin = 0;          // Event only, little-endian, bytes 4-7
    QByteArray alarmStatus;    // DoorAlarmStatus only, 4 raw bytes (bytes 0-3)
    QByteArray dssStatus;      // DoorAlarmStatus only, 4 raw bytes (bytes 4-7)
    uint8_t verified = 0;      // Event: byte 8. DoorAlarmStatus: byte 9 (not byte 8 -- see note above)
    uint8_t doorId = 0;        // Event only, byte 9
    uint8_t eventType = 0;     // byte 10, both kinds; 255 means this record is DoorAlarmStatus
    uint8_t inOutState = 0;    // Event only, byte 11
    uint32_t timeSecond = 0;   // both kinds, little-endian, bytes 12-15; raw C3DateTime-encoded
                               // seconds value, not decoded to a real date/time by this codec
};

QByteArray encodeRtlogBinaryRequest(bool includeSessionBlock, uint16_t sessionId,
                                     int32_t requestNr);

enum class RtLogStatus { Incomplete, Malformed, Rejected, NotBinaryMode, Ok };

struct RtLogBinaryResponse {
    RtLogStatus status = RtLogStatus::Incomplete;
    QVector<RtLogRecord> records;   // meaningful only when status == Ok
};
// Decodes exactly one frame from the front of `data`; does not consume/modify `data`,
// matching every other decode function in this codec. NotBinaryMode means the payload's
// byte count is not a multiple of 16 -- the panel's documented signal that binary RT log
// mode is not supported; the caller should switch to RTLOG_KEYVALUE for future requests.
RtLogBinaryResponse decodeRtlogBinaryResponse(const QByteArray &data, bool includeSessionBlock);

QByteArray encodeRtlogKeyValueRequest(bool includeSessionBlock, uint16_t sessionId,
                                       int32_t requestNr);

// Reuses the existing ResponseStatus enum -- no new states needed for this path.
struct RtLogKeyValueResponse {
    ResponseStatus status = ResponseStatus::Incomplete;
    QVector<QVariantMap> records;   // 0 or 1 records (matches zkaccess-c3-py's own
                                    // get_rt_log() behavior in key/value mode -- at most one
                                    // record per call, string-valued, un-interpreted keys)
};
RtLogKeyValueResponse decodeRtlogKeyValueResponse(const QByteArray &data, bool includeSessionBlock);

}  // namespace C3Codec
