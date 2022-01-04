#include "buffer.h"
#include <iostream>

Buffer::Buffer(int initBuffSize) : buffer(initBuffSize), readPos(0), writePos(0) {}

// 还没有被读走的字节数
size_t Buffer::readableBytes() const
{
    return writePos - readPos;
}

// writepos之后还能写的字节数
size_t Buffer::writableBytes() const
{
    return buffer.size() - writePos;
}

// readpos之前还能写的字节数（这些字节已经被读走，所以这部分是可写的）
size_t Buffer::prependableBytes() const
{
    return readPos;
}

const char *Buffer::peek() const
{
    return beginPtr() + readPos;
}

void Buffer::retrieve(size_t len)
{
    assert(len <= readableBytes());
    readPos += len;
}

void Buffer::retrieveUntil(const char *end)
{
    assert(peek() <= end );
    retrieve(end - peek());
}

void Buffer::retrieveAll()
{
    bzero(&buffer[0], buffer.size());
    readPos = 0;
    writePos = 0;
}

std::string Buffer::retrieveAllToStr()
{
    std::string str(peek(), readableBytes());
    retrieveAll();
    return str;
}

const char *Buffer::beginWriteConst() const
{
    return beginPtr() + writePos;
}

char* Buffer::beginWrite()
{
    return beginPtr() + writePos;
}

void Buffer::hasWritten(size_t len)
{
    writePos += len;
} 

void Buffer::append(const std::string& str)
{
    append(str.data(), str.length());
}

void Buffer::append(const void* data, size_t len)
{
    assert(data);
    append(static_cast<const char*>(data), len);
}

void Buffer::append(const char* str, size_t len)
{
    assert(str);
    ensureWriteable(len);
    std::copy(str, str + len, beginWrite());
    hasWritten(len);
}

void Buffer::append(const Buffer& buff)
{
    append(buff.peek(), buff.readableBytes());
}

void Buffer::ensureWriteable(size_t len)
{
    if (writableBytes() < len) {
        makeSpace(len);
    }
    assert(writableBytes() >= len);
}

ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    char buff[65535];
    struct iovec iov[2];
    const size_t writable = writableBytes();
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = beginPtr() + writePos;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);
    if (len < 0) {
        *saveErrno = errno;
    }
    else if (static_cast<size_t>(len) <= writable) {
        writePos += len;
    }
    else {
        writePos = buffer.size();
        append(buff, len - writable);
    }
    return len;
}

ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    size_t readSize = readableBytes();
    ssize_t len = write(fd, peek(), readSize);
    if (len < 0) {
        *saveErrno = errno;
        return len;
    } 
    readPos += len;
    return len;
}

char *Buffer::beginPtr()
{
    return &*buffer.begin();
}

const char *Buffer::beginPtr() const
{
    return &*buffer.begin();
}

void Buffer::makeSpace(size_t len)
{
    // 如果前后能写的总字节数小于len，直接扩展空间
    if (writableBytes() + prependableBytes() < len) {
        buffer.resize(writePos + len + 1);
    } 
    // 否则，将剩余未读走的字节前移到缓冲区首地址处
    else {
        size_t readable = readableBytes();
        std::copy(beginPtr() + readPos, beginPtr() + writePos, beginPtr());
        readPos = 0;
        writePos = readPos + readable;
        assert(readable == readableBytes());
    }
}