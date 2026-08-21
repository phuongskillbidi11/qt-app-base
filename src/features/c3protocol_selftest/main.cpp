#include "c3_codec.h"

#include <QByteArray>

#include <cstdint>
#include <cstdio>

int main() {
    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    check(C3Codec::crc16(QByteArray("123456789")) == 0xBB3D,
          "CRC-16/ARC produces the standard check value");

    const QByteArray encodedConnect =
        C3Codec::encodeConnectSessionRequest({});
    check(encodedConnect == QByteArray::fromHex("AA01760400FEFEFEFE467755"),
          "encode produces the known CONNECT_SESSION request");

    const QByteArray validFrame =
        QByteArray::fromHex("AA01C80400D6CD0000203355");
    const C3Codec::ConnectSessionResponse valid =
        C3Codec::decodeConnectSessionResponse(validFrame);
    check(valid.status == C3Codec::ResponseStatus::Ok && valid.sessionId == 0xCDD6,
          "decode returns Ok with the known session ID");

    const C3Codec::ConnectSessionResponse truncated =
        C3Codec::decodeConnectSessionResponse(validFrame.left(4));
    check(truncated.status == C3Codec::ResponseStatus::Incomplete,
          "decode returns Incomplete for a strict frame prefix");

    QByteArray rejectedFrame = validFrame;
    rejectedFrame[2] = static_cast<char>(C3Codec::kReplyError);
    const uint16_t rejectedCrc = C3Codec::crc16(rejectedFrame.mid(1, 8));
    rejectedFrame[9] = static_cast<char>(rejectedCrc & 0xFF);
    rejectedFrame[10] = static_cast<char>((rejectedCrc >> 8) & 0xFF);
    const C3Codec::ConnectSessionResponse rejected =
        C3Codec::decodeConnectSessionResponse(rejectedFrame);
    check(rejected.status == C3Codec::ResponseStatus::Rejected,
          "decode returns Rejected for a well-formed error reply");

    const QByteArray encodedDisconnect =
        C3Codec::encodeDisconnectRequest({0xCDD6, 1});
    check(encodedDisconnect == QByteArray::fromHex("AA01020400D6CD01004BAF55"),
          "encode produces the known DISCONNECT request");

    check(C3Codec::encodeConnectSessionLessRequest({}) ==
              QByteArray::fromHex("AA01010000503C55"),
          "encode produces the real device's accepted CONNECT_SESSION_LESS request");

    check(C3Codec::decodeGenericReply(QByteArray::fromHex("AA01C80000800255")).status ==
              C3Codec::ResponseStatus::Ok,
          "decode reads the real device's CONNECT_SESSION_LESS reply as Ok");

    check(C3Codec::decodeGenericReply(QByteArray::fromHex("AA01C90100F313D955")).status ==
              C3Codec::ResponseStatus::Rejected,
          "decode reads the real device's CONNECT_SESSION rejection as Rejected via the generic path");

    check(C3Codec::encodeSessionLessDisconnectRequest() ==
              QByteArray::fromHex("AA01020000A03C55"),
          "encode produces the real device's accepted session-less DISCONNECT request");

    check(C3Codec::encodeDataTableConfigRequest(false, 0, 0) ==
              QByteArray::fromHex("AA01060000E1FD55"),
          "encode produces the session-less DATATABLE_CFG request");

    {
        const QByteArray cfgReply = QByteArray::fromHex(
            "AA01C86600757365723D312C5549443D69312C436172644E6F3D69322C50696E3D69332C50617373"
            "776F72643D73342C47726F75703D69352C537461727454696D653D69362C456E6454696D653D6937"
            "2C4E616D653D73382C5375706572417574686F72697A653D69390A821055");
        const C3Codec::DataTableConfigResponse cfg =
            C3Codec::decodeDataTableConfigResponse(cfgReply, false);
        check(cfg.status == C3Codec::ResponseStatus::Ok && cfg.tables.size() == 1 &&
                  cfg.tables[0].name == "user" && cfg.tables[0].index == 1 &&
                  cfg.tables[0].fields.size() == 9 &&
                  cfg.tables[0].fields[0].name == "UID" && cfg.tables[0].fields[0].type == 'i' &&
                  cfg.tables[0].fields[0].index == 1 &&
                  cfg.tables[0].fields[3].name == "Password" && cfg.tables[0].fields[3].type == 's' &&
                  cfg.tables[0].fields[8].name == "SuperAuthorize" && cfg.tables[0].fields[8].index == 9,
              "decode reads the real 'user' table config line correctly");
    }

    check(C3Codec::encodeGetDataRequest(1, {1,2,3,4,5,6,7,8,9}, false, 0, 0) ==
              QByteArray::fromHex("AA01080D0001090102030405060708090000073F55"),
          "encode produces the session-less GETDATA request for the user table");

    {
        const QVector<C3Codec::DataTableField> userFields = {
            {"UID", 'i', 1}, {"CardNo", 'i', 2}, {"Pin", 'i', 3}, {"Password", 's', 4},
            {"Group", 'i', 5}, {"StartTime", 'i', 6}, {"EndTime", 'i', 7}, {"Name", 's', 8},
            {"SuperAuthorize", 'i', 9}};
        const QByteArray dataReply = QByteArray::fromHex(
            "AA01C83900010901020304050607080901010387D61203765432000100010001000001000102"
            "03A1A3A303B1B2B3000100042A893401049FB0340100010082CE55");
        const C3Codec::GetDataResponse data =
            C3Codec::decodeGetDataResponse(dataReply, 1, userFields, false);
        check(data.status == C3Codec::GetDataStatus::Ok && data.records.size() == 2 &&
                  data.records[0]["UID"].toInt() == 1 &&
                  data.records[0]["CardNo"].toLongLong() == 1234567 &&
                  data.records[1]["StartTime"].toLongLong() == 20220202 &&
                  data.records[1]["EndTime"].toLongLong() == 20230303,
              "decode reads the real 2-record user-table fixture correctly");
    }

    {
        const QByteArray txnCfgReply = QByteArray::fromHex(
            "AA01C85F007472616E73616374696F6E3D352C436172646E6F3D69312C50696E3D69322C56657269"
            "666965643D69332C446F6F7249443D69342C4576656E74547970653D69352C496E4F7574537461746"
            "53D69362C54696D655F7365636F6E643D69370A8E7555");
        const C3Codec::DataTableConfigResponse cfg =
            C3Codec::decodeDataTableConfigResponse(txnCfgReply, false);
        check(cfg.status == C3Codec::ResponseStatus::Ok && cfg.tables.size() == 1 &&
                  cfg.tables[0].name == "transaction" && cfg.tables[0].index == 5 &&
                  cfg.tables[0].fields.size() == 7 &&
                  cfg.tables[0].fields[6].name == "Time_second" && cfg.tables[0].fields[6].type == 'i' &&
                  cfg.tables[0].fields[6].index == 7,
              "decode reads the real 'transaction' table config line correctly");
    }

    {
        const QVector<C3Codec::DataTableField> txnFields = {
            {"Cardno", 'i', 1}, {"Pin", 'i', 2}, {"Verified", 'i', 3}, {"DoorID", 'i', 4},
            {"EventType", 'i', 5}, {"InOutState", 'i', 6}, {"Time_second", 'i', 7}};
        const QByteArray txnDataReply = QByteArray::fromHex(
            "AA01C81A00050701020304050607016401010101010201030101041F3ABA14941955");
        const C3Codec::GetDataResponse data =
            C3Codec::decodeGetDataResponse(txnDataReply, 5, txnFields, false);
        check(data.status == C3Codec::GetDataStatus::Ok && data.records.size() == 1 &&
                  data.records[0]["Cardno"].toLongLong() == 100 &&
                  data.records[0]["DoorID"].toInt() == 2 &&
                  data.records[0]["Time_second"].toLongLong() == 347748895,
              "decode reads a self-authored 'transaction' record correctly (Time_second matches "
              "utils.py's own worked C3DateTime example, 2010-10-26 20:54:55)");
    }

    {
        const QByteArray tplCfgReply = QByteArray::fromHex(
            "AA01C83B0074656D706C6174653D392C53697A653D69312C50696E3D69322C46696E67657249443D"
            "69332C56616C69643D69342C54656D706C6174653D73350AFD4B55");
        const C3Codec::DataTableConfigResponse cfg =
            C3Codec::decodeDataTableConfigResponse(tplCfgReply, false);
        check(cfg.status == C3Codec::ResponseStatus::Ok && cfg.tables.size() == 1 &&
                  cfg.tables[0].name == "template" && cfg.tables[0].index == 9 &&
                  cfg.tables[0].fields.size() == 5 &&
                  cfg.tables[0].fields[4].name == "Template" && cfg.tables[0].fields[4].type == 's' &&
                  cfg.tables[0].fields[4].index == 5,
              "decode reads the real 'template' table config line correctly");
    }

    {
        const QVector<C3Codec::DataTableField> tplFields = {
            {"Size", 'i', 1}, {"Pin", 'i', 2}, {"FingerID", 'i', 3}, {"Valid", 'i', 4},
            {"Template", 's', 5}};
        const QByteArray tplDataReply = QByteArray::fromHex(
            "AA01C813000905010203040501" "3C0101010001010341424341BF55");
        const C3Codec::GetDataResponse data =
            C3Codec::decodeGetDataResponse(tplDataReply, 9, tplFields, false);
        check(data.status == C3Codec::GetDataStatus::Ok && data.records.size() == 1 &&
                  data.records[0]["Size"].toInt() == 60 &&
                  data.records[0]["Template"].toString() == "ABC",
              "decode reads a self-authored 'template' record correctly (string-typed field)");
    }

    {
        const QByteArray realTxnCfgReply = QByteArray::fromHex(
            "AA01C874007472616E73616374696F6E3D352C50696E3D73312C56657269666965643D69322C446F"
            "6F7249443D69332C4576656E74547970653D69342C496E4F757453746174653D69352C54696D655F"
            "7365636F6E643D69362C496E6465783D69372C436172646E6F3D4C382C53697465636F64653D6939"
            "0A94EA55");
        const C3Codec::DataTableConfigResponse cfg =
            C3Codec::decodeDataTableConfigResponse(realTxnCfgReply, false);
        check(cfg.status == C3Codec::ResponseStatus::Ok && cfg.tables.size() == 1 &&
                  cfg.tables[0].name == "transaction" && cfg.tables[0].fields.size() == 9 &&
                  cfg.tables[0].fields[7].name == "Cardno" && cfg.tables[0].fields[7].type == 'L' &&
                  cfg.tables[0].fields[7].index == 8,
              "decode reads the real live-captured 'transaction' schema, including the 'L' field type");

        const QByteArray dataReply = QByteArray::fromHex(
            "AA01C82200050901020304050607080901310101010101000100041F3ABA14010103"
            "87D6120101ADAC55");
        const C3Codec::GetDataResponse data =
            C3Codec::decodeGetDataResponse(dataReply, 5, cfg.tables[0].fields, false);
        check(data.status == C3Codec::GetDataStatus::Ok && data.records.size() == 1 &&
                  data.records[0]["Cardno"].toLongLong() == 1234567 &&
                  data.records[0]["Time_second"].toLongLong() == 347748895,
              "decode reads an 'L'-typed Cardno field the same way as an 'i' field");
    }

    {
        const QVector<C3Codec::DataTableField> txnFieldsReal = {
            {"Pin", 's', 1}, {"Verified", 'i', 2}, {"DoorID", 'i', 3}, {"EventType", 'i', 4},
            {"InOutState", 'i', 5}, {"Time_second", 'i', 6}, {"Index", 'i', 7},
            {"Cardno", 'L', 8}, {"Sitecode", 'i', 9}};
        const QByteArray realOddReply = QByteArray::fromHex("AA200D1100005D8900005D89000075209643F4130000951655");
        const C3Codec::GetDataResponse data =
            C3Codec::decodeGetDataResponse(realOddReply, 5, txnFieldsReal, false);
        check(data.status == C3Codec::GetDataStatus::TableMismatch,
              "a real, unrecognized-command reply (0x0D, live-captured) is now a structurally-"
              "valid TableMismatch instead of a blanket Malformed");
    }

    check(C3Codec::encodeRtlogBinaryRequest(false, 0, 0) ==
              QByteArray::fromHex("AA010B0000703E55"),
          "encode produces the session-less RTLOG_BINARY request");

    {
        const QByteArray eventReply = QByteArray::fromHex(
            "AA01C81000014F86009992980004010000A5ADAD211EA055");
        const C3Codec::RtLogBinaryResponse resp =
            C3Codec::decodeRtlogBinaryResponse(eventReply, false);
        check(resp.status == C3Codec::RtLogStatus::Ok && resp.records.size() == 1 &&
                  resp.records[0].kind == C3Codec::RtLogRecordKind::Event &&
                  resp.records[0].cardNo == 8802049 && resp.records[0].pin == 9999001 &&
                  resp.records[0].verified == 4 && resp.records[0].doorId == 1 &&
                  resp.records[0].eventType == 0 && resp.records[0].inOutState == 0 &&
                  resp.records[0].timeSecond == 565030309,
              "decode reads the reference project's own real docstring Event record correctly");
    }

    {
        const QByteArray doorAlarmReply = QByteArray::fromHex(
            "AA01C81000014F8600999298000401FF00A5ADAD210AAF55");
        const C3Codec::RtLogBinaryResponse resp =
            C3Codec::decodeRtlogBinaryResponse(doorAlarmReply, false);
        check(resp.status == C3Codec::RtLogStatus::Ok && resp.records.size() == 1 &&
                  resp.records[0].kind == C3Codec::RtLogRecordKind::DoorAlarmStatus &&
                  resp.records[0].alarmStatus == QByteArray::fromHex("014F8600") &&
                  resp.records[0].dssStatus == QByteArray::fromHex("99929800") &&
                  resp.records[0].verified == 1 && resp.records[0].eventType == 255 &&
                  resp.records[0].timeSecond == 565030309,
              "decode reads a DoorAlarmStatus record correctly, following the reference "
              "project's own code (verified=byte 9), not its docstring (byte 8)");
    }

    {
        const QByteArray twoRecordReply = QByteArray::fromHex(
            "AA01C82000014F86009992980004010000A5ADAD21014F8600999298000401FF00A5ADAD21CEEB55");
        const C3Codec::RtLogBinaryResponse resp =
            C3Codec::decodeRtlogBinaryResponse(twoRecordReply, false);
        check(resp.status == C3Codec::RtLogStatus::Ok && resp.records.size() == 2 &&
                  resp.records[0].kind == C3Codec::RtLogRecordKind::Event &&
                  resp.records[1].kind == C3Codec::RtLogRecordKind::DoorAlarmStatus,
              "decode reads two back-to-back 16-byte records correctly, in order");
    }

    check(C3Codec::decodeRtlogBinaryResponse(QByteArray::fromHex("AA01C8050001020304055BDD55"), false)
              .status == C3Codec::RtLogStatus::NotBinaryMode,
          "decode recognizes a non-multiple-of-16 payload as NotBinaryMode");

    check(C3Codec::decodeRtlogBinaryResponse(QByteArray::fromHex("AA01C80000800255"), false).status ==
              C3Codec::RtLogStatus::Ok,
          "decode treats a zero-length payload as Ok with zero records, not NotBinaryMode");

    {
        const QByteArray kvReply = QByteArray::fromHex(
            "AA01C860007469"
            "6D653D323032332D31322D30362032323A33333A31352C70696E3D302C636172646E6F3D302C6576"
            "656E74616464723D312C6576656E743D382C696E6F75747374617475733D322C766572696679747970"
            "653D3230302C696E6465783D39E22E55");
        const C3Codec::RtLogKeyValueResponse resp =
            C3Codec::decodeRtlogKeyValueResponse(kvReply, false);
        check(resp.status == C3Codec::ResponseStatus::Ok && resp.records.size() == 1 &&
                  resp.records[0]["cardno"].toString() == "0" &&
                  resp.records[0]["event"].toString() == "8",
              "decode reads a self-authored key/value RT log record correctly");
    }

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::Output, 1,
              static_cast<uint8_t>(C3Codec::ControlOutputAddress::Door), 3, false, 0, 0) ==
              QByteArray::fromHex("AA010505000101010300F5A355"),
          "encode produces a real, timed door-unlock CONTROL request (door 1, 3s)");

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::Output, 1,
              static_cast<uint8_t>(C3Codec::ControlOutputAddress::Door), 0, false, 0, 0) ==
              QByteArray::fromHex("AA010505000101010000F55355"),
          "encode produces a door-close (duration=0) CONTROL request");

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::Output, 1,
              static_cast<uint8_t>(C3Codec::ControlOutputAddress::Door), 255, false, 0, 0) ==
              QByteArray::fromHex("AA01050500010101FF00B4A355"),
          "encode produces a normal-open (duration=255) CONTROL request");

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::Output, 1,
              static_cast<uint8_t>(C3Codec::ControlOutputAddress::Aux), 5, false, 0, 0) ==
              QByteArray::fromHex("AA010505000101020500060355"),
          "encode produces an aux-output CONTROL request");

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::CancelAlarm, 0, 0, 0, false, 0, 0) ==
              QByteArray::fromHex("AA010505000200000000E16F55"),
          "encode produces a CANCEL_ALARM CONTROL request");

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::RestartDevice, 0, 0, 0, false, 0, 0) ==
              QByteArray::fromHex("AA010505000300000000DCAF55"),
          "encode produces a RESTART_DEVICE CONTROL request (encode-only -- never sent live)");

    check(C3Codec::encodeControlRequest(C3Codec::ControlOperation::EnableDisableNormalOpen, 1, 1, 0,
              false, 0, 0) == QByteArray::fromHex("AA010505000401010000395355"),
          "encode produces an EnableDisableNormalOpen CONTROL request (encode-only -- never sent live)");

    check(C3Codec::encodeGetParamRequest(
              {"~SerialNumber", "FirmVer", "DeviceName", "LockCount", "AuxInCount", "AuxOutCount"},
              false, 0, 0) ==
              QByteArray::fromHex(
                  "AA010441007E53657269616C4E756D6265722C4669726D5665722C4465766963654E616D652C"
                  "4C6F636B436F756E742C417578496E436F756E742C4175784F7574436F756E74D06355"),
          "encode produces the same six-parameter GETPARAM request core.py's own "
          "_initialize() sends");

    {
        const QByteArray reply = QByteArray::fromHex(
            "AA01C85D007E53657269616C4E756D6265723D424A313233343536372C4669726D5665723D362E"
            "36302C4465766963654E616D653D43332D3430302C4C6F636B436F756E743D342C417578496E43"
            "6F756E743D342C4175784F7574436F756E743D34839E55");
        const C3Codec::GetParamResponse resp = C3Codec::decodeGetParamResponse(reply, false);
        check(resp.status == C3Codec::ResponseStatus::Ok && resp.values.size() == 6 &&
                  resp.values["~SerialNumber"].toString() == "BJ1234567" &&
                  resp.values["FirmVer"].toString() == "6.60" &&
                  resp.values["DeviceName"].toString() == "C3-400" &&
                  resp.values["LockCount"].toString() == "4" &&
                  resp.values["AuxInCount"].toString() == "4" &&
                  resp.values["AuxOutCount"].toString() == "4",
              "decode reads a self-authored six-parameter GETPARAM response correctly");
    }

    return allPassed ? 0 : 1;
}
