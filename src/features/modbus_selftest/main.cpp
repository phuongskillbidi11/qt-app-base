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

    ModbusCodec::WriteSingleRegisterRequest writeRequest;
    writeRequest.transactionId = 1;
    writeRequest.unitId = 1;
    writeRequest.address = 1;
    writeRequest.value = 3;
    const QByteArray encodedWrite = ModbusCodec::encodeWriteSingleRegisterRequest(writeRequest);
    check(encodedWrite == QByteArray::fromHex("000100000006010600010003"),
          "encode produces the known Write Single Register request");

    const QByteArray writeOkFrame = QByteArray::fromHex("000100000006010600010003");
    const ModbusCodec::WriteSingleRegisterResponse writeOk =
        ModbusCodec::decodeWriteSingleRegisterResponse(writeOkFrame);
    check(writeOk.status == ModbusCodec::ResponseStatus::Ok
              && writeOk.transactionId == 1
              && writeOk.address == 1
              && writeOk.value == 3,
          "decode returns Ok with the echoed address and value");

    const ModbusCodec::WriteSingleRegisterResponse writeException =
        ModbusCodec::decodeWriteSingleRegisterResponse(
            QByteArray::fromHex("000100000003018602"));
    check(writeException.status == ModbusCodec::ResponseStatus::Exception
              && writeException.exceptionCode == 2,
          "decode returns Exception with the known exception code for a write");

    ModbusCodec::ReadInputRegistersRequest inputRequest;
    inputRequest.transactionId = 1;
    inputRequest.unitId = 1;
    inputRequest.startAddress = 0;
    inputRequest.quantity = 2;
    const QByteArray encodedInput = ModbusCodec::encodeReadInputRegistersRequest(inputRequest);
    check(encodedInput == QByteArray::fromHex("000100000006010400000002"),
          "encode produces the known Read Input Registers request");

    const ModbusCodec::ReadInputRegistersResponse inputValid =
        ModbusCodec::decodeReadInputRegistersResponse(
            QByteArray::fromHex("000100000007010404006400c8"));
    const bool inputRegistersMatch = inputValid.registers.size() == 2
        && inputValid.registers.at(0) == 100 && inputValid.registers.at(1) == 200;
    check(inputValid.status == ModbusCodec::ResponseStatus::Ok
              && inputValid.transactionId == 1
              && inputRegistersMatch,
          "decode returns Ok with the known Input Register values");

    ModbusCodec::WriteMultipleRegistersRequest writeMultipleRequest;
    writeMultipleRequest.transactionId = 1;
    writeMultipleRequest.unitId = 1;
    writeMultipleRequest.startAddress = 1;
    writeMultipleRequest.values = {10, 20};
    const QByteArray encodedWriteMultiple =
        ModbusCodec::encodeWriteMultipleRegistersRequest(writeMultipleRequest);
    check(encodedWriteMultiple == QByteArray::fromHex("00010000000b01100001000204000a0014"),
          "encode produces the known Write Multiple Registers request");

    const QByteArray writeMultipleOkFrame = QByteArray::fromHex("000100000006011000010002");
    const ModbusCodec::WriteMultipleRegistersResponse writeMultipleOk =
        ModbusCodec::decodeWriteMultipleRegistersResponse(writeMultipleOkFrame);
    check(writeMultipleOk.status == ModbusCodec::ResponseStatus::Ok
              && writeMultipleOk.transactionId == 1
              && writeMultipleOk.startAddress == 1
              && writeMultipleOk.quantity == 2,
          "decode returns Ok with the echoed start address and quantity");

    const ModbusCodec::WriteMultipleRegistersResponse writeMultipleException =
        ModbusCodec::decodeWriteMultipleRegistersResponse(
            QByteArray::fromHex("000100000003019002"));
    check(writeMultipleException.status == ModbusCodec::ResponseStatus::Exception
              && writeMultipleException.exceptionCode == 2,
          "decode returns Exception with the known exception code for a multi-register write");

    return allPassed ? 0 : 1;
}
