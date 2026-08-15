#include "mq_buffer.h"

void MqBuffer::feed(
    QByteArray &buffer,
    const QByteArray &chunk,
    const std::function<quint64(const char *, quint64)> &consume) {
    buffer.append(chunk);
    while (!buffer.isEmpty()) {
        const quint64 consumed =
            consume(buffer.constData(), static_cast<quint64>(buffer.size()));
        if (consumed == 0) {
            return;
        }
        buffer.remove(0, static_cast<int>(consumed));
    }
}
