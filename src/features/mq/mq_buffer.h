#pragma once

#include <QByteArray>

#include <functional>

namespace MqBuffer {

// Appends chunk, consumes every complete record, and retains any partial remainder.
void feed(QByteArray &buffer,
          const QByteArray &chunk,
          const std::function<quint64(const char *, quint64)> &consume);

}  // namespace MqBuffer
