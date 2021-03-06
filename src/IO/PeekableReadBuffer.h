#pragma once
#include <IO/ReadBuffer.h>
#include <IO/BufferWithOwnMemory.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

/// Also allows to set checkpoint at some position in stream and come back to this position later.
/// When next() is called, saves data between checkpoint and current position to own memory and loads next data to sub-buffer
/// Sub-buffer should not be accessed directly during the lifetime of peekable buffer.
/// If position() of peekable buffer is explicitly set to some position before checkpoint
/// (e.g. by istr.position() = prev_pos), behavior is undefined.
class PeekableReadBuffer : public BufferWithOwnMemory<ReadBuffer>
{
    friend class PeekableReadBufferCheckpoint;
public:
    explicit PeekableReadBuffer(ReadBuffer & sub_buf_, size_t start_size_ = DBMS_DEFAULT_BUFFER_SIZE,
                                                       size_t unread_limit_ = 16 * DBMS_DEFAULT_BUFFER_SIZE);

    ~PeekableReadBuffer() override;

    /// Sets checkpoint at current position
    ALWAYS_INLINE inline void setCheckpoint()
    {
#ifndef NDEBUG
        if (checkpoint)
            throw DB::Exception("Does not support recursive checkpoints.", ErrorCodes::LOGICAL_ERROR);
#endif
        checkpoint_in_own_memory = currentlyReadFromOwnMemory();
        if (!checkpoint_in_own_memory)
        {
            /// Don't need to store unread data anymore
            peeked_size = 0;
        }
        checkpoint = pos;
    }

    /// Forget checkpoint and all data between checkpoint and position
    ALWAYS_INLINE inline void dropCheckpoint()
    {
#ifndef NDEBUG
        if (!checkpoint)
            throw DB::Exception("There is no checkpoint", ErrorCodes::LOGICAL_ERROR);
#endif
        if (!currentlyReadFromOwnMemory())
        {
            /// Don't need to store unread data anymore
            peeked_size = 0;
        }
        checkpoint = nullptr;
        checkpoint_in_own_memory = false;
    }

    /// Sets position at checkpoint.
    /// All pointers (such as this->buffer().end()) may be invalidated
    void rollbackToCheckpoint();

    /// If checkpoint and current position are in different buffers, appends data from sub-buffer to own memory,
    /// so data between checkpoint and position will be in continuous memory.
    void makeContinuousMemoryFromCheckpointToPos();

    /// Returns true if there unread data extracted from sub-buffer in own memory.
    /// This data will be lost after destruction of peekable buffer.
    bool hasUnreadData() const;

private:
    bool nextImpl() override;

    bool peekNext();

    inline bool useSubbufferOnly() const { return !peeked_size; }
    inline bool currentlyReadFromOwnMemory() const { return working_buffer.begin() != sub_buf.buffer().begin(); }
    inline bool checkpointInOwnMemory() const { return checkpoint_in_own_memory; }

    void checkStateCorrect() const;

    /// Makes possible to append `bytes_to_append` bytes to data in own memory.
    /// Updates all invalidated pointers and sizes.
    void resizeOwnMemoryIfNecessary(size_t bytes_to_append);


    ReadBuffer & sub_buf;
    const size_t unread_limit;
    size_t peeked_size = 0;
    Position checkpoint = nullptr;
    bool checkpoint_in_own_memory = false;
};


class PeekableReadBufferCheckpoint : boost::noncopyable
{
    PeekableReadBuffer & buf;
    bool auto_rollback;
public:
    explicit PeekableReadBufferCheckpoint(PeekableReadBuffer & buf_, bool auto_rollback_ = false)
                : buf(buf_), auto_rollback(auto_rollback_) { buf.setCheckpoint(); }
    ~PeekableReadBufferCheckpoint()
    {
        if (!buf.checkpoint)
            return;
        if (auto_rollback)
            buf.rollbackToCheckpoint();
        buf.dropCheckpoint();
    }

};

}
