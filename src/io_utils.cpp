#include "../include/io_utils.h"
#include "../include/coroutine_utils.h"

QTNETWORKNG_NAMESPACE_BEGIN


FileLike::~FileLike() {}


QByteArray FileLike::readall(bool *ok)
{
    QByteArray data;
    qint64 s = size();
    if (s >= static_cast<qint64>(INT32_MAX)) {
        if (ok) *ok = false;
        return data;
    } else if (s == 0) {
        return data;
    } else if (s < 0) {
        // size() is not supported.
    } else { // 0 < s < INT32_MAX
        data.reserve(static_cast<qint32>(s));
    }
    char buf[1024 * 8];
    while (true) {
        qint32 readBytes = read(buf, 1024 * 8);
        if (readBytes <= 0) {
            if (ok) *ok = (s < 0 || data.size() == s);
            return data;
        }
        data.append(buf, readBytes);
    }
}


class RawFile: public FileLike
{
public:
    RawFile(QSharedPointer<QFile> f)
        :f(f) {}
    virtual qint32 read(char *data, qint32 size) override;
    virtual qint32 write(const char *data, qint32 size) override;
    virtual void close() override;
    virtual qint64 size() override;
private:
    QSharedPointer<QFile> f;
};


qint32 RawFile::read(char *data, qint32 size)
{
    qint64 len = f->read(data, size);
    return static_cast<qint32>(len);
}


qint32 RawFile::write(const char *data, qint32 size)
{
    qint64 len = f->write(data, size);
    return static_cast<qint32>(len);
}


void RawFile::close()
{
    f->close();
}


qint64 RawFile::size()
{
    return f->size();
}


QSharedPointer<FileLike> FileLike::rawFile(QSharedPointer<QFile> f)
{
    return QSharedPointer<RawFile>::create(f).dynamicCast<FileLike>();
}


class BytesIOPrivate
{
public:
    BytesIOPrivate(const QByteArray &buf)
        :buf(buf), pos(0) {}
    BytesIOPrivate()
        :pos(0) {}
    QByteArray buf;
    qint32 pos;
};


BytesIO::BytesIO(const QByteArray &buf)
    :d_ptr(new BytesIOPrivate(buf))
{

}


BytesIO::BytesIO()
    :d_ptr(new BytesIOPrivate())
{

}


BytesIO::~BytesIO()
{
    delete d_ptr;
}


qint32 BytesIO::read(char *data, qint32 size)
{
    Q_D(BytesIO);
    qint32 leftBytes = qMax(d->buf.size() - d->pos, 0);
    qint32 readBytes = qMin(leftBytes, size);
    memcpy(data, d->buf.data() + d->pos, static_cast<size_t>(readBytes));
    d->pos += readBytes;
    return readBytes;
}


qint32 BytesIO::write(const char *data, qint32 size)
{
    Q_D(BytesIO);
    if (d->pos + size > d->buf.size()) {
        d->buf.resize(d->pos + size);
    }
    memcpy(d->buf.data() + d->pos, data, static_cast<size_t>(size));
    d->pos += size;
    return size;
}


void BytesIO::close()
{

}


qint64 BytesIO::size()
{
    Q_D(BytesIO);
    return d->buf.size();
}


QByteArray BytesIO::data()
{
    Q_D(BytesIO);
    return d->buf;
}


QSharedPointer<FileLike> FileLike::bytes(const QByteArray &data)
{
    return QSharedPointer<BytesIO>::create(data).dynamicCast<FileLike>();
}


bool sendfile(QSharedPointer<FileLike> inputFile, QSharedPointer<FileLike> outputFile, qint64 size)
{
    if (inputFile.isNull() || outputFile.isNull()) {
        return false;
    }
    if (size < 0) {
        size = inputFile->size();
        if (size == 0) {
            return true;
        } else if (size < 0) {
            // size() is not supported.
        }
    }
    QByteArray buf;
    QByteArray t(1024 * 8, Qt::Uninitialized);
    qint64 total = 0;
    bool eof = false;
    while (true) {
        qint64 remain = INT64_MAX;
        if (size > 0) {
            remain = qMax<qint64>(0, size - buf.size() - total);
        }
        if (remain > 0) {
            qint32 nextBlockSize = 1024 * 16 - buf.size();
            if (!eof && nextBlockSize > 1024 * 8) {
                nextBlockSize = qMin(static_cast<qint64>(nextBlockSize), remain);
                qint32 readBytes = inputFile->read(t.data(), nextBlockSize);
                if (readBytes < 0) {
                    return false;
                } else if (readBytes > 0) {
                    total += readBytes;
                    buf.append(t.data(), readBytes);
                } else {
                    eof = true;
                }
            }
        }
        if (buf.isEmpty()) {
            return size < 0 || total == size;
        } else {
            qint32 writtenBytes = outputFile->write(buf, buf.size());
            if (writtenBytes <= 0) {
                return false;
            }
            buf.remove(0, writtenBytes);
        }
    }
}


QTNETWORKNG_NAMESPACE_END
