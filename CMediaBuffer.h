#include <dmo.h>

class CMediaBuffer: public IMediaBuffer {
    std::vector<BYTE> m_buffer;
    DWORD m_count;
    LONG m_refcount;
public:
    static HRESULT Create(LONG capacity, IMediaBuffer **buffer)
    {
        *buffer = new CMediaBuffer(capacity);
        return S_OK;
    }
    STDMETHODIMP_(HRESULT) QueryInterface(REFIID riid, void **ppv)
    {
        if (riid == IID_IMediaBuffer || riid == IID_IUnknown) {
            *ppv = static_cast<IMediaBuffer*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = 0;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_refcount);
    }
    STDMETHODIMP_(ULONG) Release()
    {
        LONG nref = InterlockedDecrement(&m_refcount);
        if (!nref)
            delete this;
        return nref;
    }
    STDMETHODIMP SetLength(DWORD len)
    {
        m_count = len;
        return S_OK;
    }
    STDMETHODIMP GetMaxLength(DWORD *len)
    {
        *len = m_buffer.size();
        return S_OK;
    }
    STDMETHODIMP GetBufferAndLength(BYTE **buffer, DWORD *len)
    {
        if (buffer)
            *buffer = &m_buffer[0];
        if (len)
            *len = m_count;
        return S_OK;
    }
private:
    CMediaBuffer(DWORD capacity):
        m_buffer(capacity), m_count(0), m_refcount(1)
    {
    }
    ~CMediaBuffer()
    {
    }
};
