#ifndef NativeBuffer_h
#define NativeBuffer_h

#include <vector>
#include <stdint.h>

namespace motioncam {
    class NativeBuffer {
    public:
        NativeBuffer() : mValidStart{0}, mValidEnd{0} {
            
        }
        virtual ~NativeBuffer() {}

        virtual uint8_t* lock(bool write) = 0;
        virtual void unlock() = 0;
        virtual uint64_t nativeHandle() = 0;
        virtual size_t len() = 0;
        virtual const std::vector<uint8_t>& hostData() = 0;
        virtual void copyHostData(const std::vector<uint8_t>& data) = 0;
        virtual void release() = 0;
        virtual std::unique_ptr<NativeBuffer> clone() = 0;
        virtual void shrink(size_t newSize) = 0;
        
        void setValidRange(size_t start, size_t end) {
            mValidStart = start;
            mValidEnd = end;
        }
        
        void getValidRange(size_t& outStart, size_t& outEnd) {
            if(mValidStart == mValidEnd) {
                outStart = 0;
                outEnd = len();
            }
            else {
                outStart = mValidStart;
                outEnd = mValidEnd;
            }
        }
        
    private:
        size_t mValidStart;
        size_t mValidEnd;
    };

    class NativeHostBuffer : public NativeBuffer {
    public:
        NativeHostBuffer()
        {
        }

        NativeHostBuffer(size_t length) : data(length)
        {
        }

        NativeHostBuffer(const std::vector<uint8_t>& other) : data(other)
        {
        }

        NativeHostBuffer(const uint8_t* other, size_t len)
        {
            data.resize(len);
            data.assign(other, other + len);
        }

        std::unique_ptr<NativeBuffer> clone() {
            return std::unique_ptr<NativeHostBuffer>(new NativeHostBuffer(data));
        }

        uint8_t* lock(bool write) {
            return data.data();
        }
        
        void unlock() {
        }
        
        uint64_t nativeHandle() {
            return 0;
        }
        
        size_t len() {
            return data.size();
        }
        
        void allocate(size_t len) {
            data.resize(len);
        }
        
        const std::vector<uint8_t>& hostData()
        {
            return data;
        }
        
        void copyHostData(const std::vector<uint8_t>& other)
        {
            data = std::move(other);
        }
        
        void release()
        {
            data.resize(0);
            data.shrink_to_fit();
        }

        void shrink(size_t newSize)
        {
            data.resize(newSize);
        }

    private:
        std::vector<uint8_t> data;
    };

} // namespace motioncam

#endif /* NativeBuffer_h */
