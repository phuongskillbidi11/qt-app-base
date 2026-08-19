#include "modbus_codec.h"

#include <QByteArray>
#include <QVector>

#include <cstdint>
#include <cstdio>

int main() {
    bool allPassed = true;
    const auto check = [&allPassed](bool passed, const char *message) {
        std::printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        allPassed = allPassed && passed;
    };

    ModbusCodec::ReadHoldingRegistersRequest request;
    request.transactionId = 1;
    request.unitId = 1;
    request.startAddress = 0;
    request.quantity = 1;
    const QByteArray encoded = ModbusCodec::encodeReadHoldingRegistersRequest(request);
    check(encoded == QByteArray::fromHex("000100000006010300000001"),
          "encode produces the known Read Holding Registers request");

    const QByteArray validFrame = QByteArray::fromHex("0001000000050103020064");
    const ModbusCodec::ReadHoldingRegistersResponse valid =
        ModbusCodec::decodeReadHoldingRegistersResponse(validFrame);
    // Not QVector<uint16_t>::operator== -- Qt 5.15's qvector.h relies on MSVC's
    // stdext::make_checked_array_iterator for that operator, which some MSVC toolset
    // versions do not expose without an explicit <iterator> include Qt itself is missing.
    // This project's own CI hit exactly that (first-ever QVector<uint16_t> comparison in
    // the codebase); comparing manually sidesteps the Qt bug regardless of MSVC version.
    const bool registersMatch = valid.registers.size() == 1 && valid.registers.at(0) == 100;
    check(valid.status == ModbusCodec::ResponseStatus::Ok
              && valid.transactionId == 1
              && registersMatch,
          "decode returns Ok with the known register value");

    const ModbusCodec::ReadHoldingRegistersResponse truncated =
        ModbusCodec::decodeReadHoldingRegistersResponse(validFrame.left(4));
    check(truncated.status == ModbusCodec::ResponseStatus::Incomplete,
          "decode returns Incomplete for a strict frame prefix");

    const ModbusCodec::ReadHoldingRegistersResponse exception =
        ModbusCodec::decodeReadHoldingRegistersResponse(
            QByteArray::fromHex("000100000003018302"));
    check(exception.status == ModbusCodec::ResponseStatus::Exception
              && exception.exceptionCode == 2,
          "decode returns Exception with the known exception code");

    return allPassed ? 0 : 1;
}
