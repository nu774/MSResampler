#include "wavsource.h"
#include "wavsink.h"
#include "MSResampler.h"
#include "Quantizer.h"
#include "wgetopt.h"

static
void secondsToHMS(double seconds, int *h, int *m, int *s, int *millis)
{
    *h = seconds / 3600;
    seconds -= *h * 3600;
    *m = seconds / 60;
    seconds -= *m * 60;
    *s = seconds;
    *millis = (seconds - *s) * 1000;
}

static
std::wstring formatSeconds(double seconds)
{
    int h, m, s, millis;
    secondsToHMS(seconds, &h, &m, &s, &millis);
    return h ? strutil::format(L"%d:%02d:%02d.%03d", h, m, s, millis)
             : strutil::format(L"%d:%02d.%03d", m, s, millis);
}

class Timer {
    DWORD m_ticks;
public:
    Timer() { m_ticks = GetTickCount(); };
    double ellapsed() {
        return (static_cast<double>(GetTickCount()) - m_ticks) / 1000.0;
    }
};

class PeriodicDisplay {
    uint32_t m_interval;
    uint32_t m_last_tick;
    std::wstring m_message;
public:
    PeriodicDisplay(uint32_t interval)
        : m_interval(interval),
          m_last_tick(GetTickCount())
    {
    }
    void put(const std::wstring &message) {
        m_message = message;
        uint32_t tick = GetTickCount();
        if (tick - m_last_tick > m_interval) {
            flush();
            m_last_tick = tick;
        }
    }
    void flush() {
        std::fputws(m_message.c_str(), stderr);
    }
};

class Progress {
    PeriodicDisplay m_disp;
    uint64_t m_total;
    uint32_t m_rate;
    std::wstring m_tstamp;
    Timer m_timer;
    bool m_console_visible;
public:
    Progress(uint64_t total, uint32_t rate)
        : m_disp(100),
          m_total(total), m_rate(rate)
    {
        if (total != ~0ULL)
            m_tstamp = formatSeconds(static_cast<double>(total) / rate);
    }
    void update(uint64_t current)
    {
        double fcurrent = current;
        double percent = 100.0 * fcurrent / m_total;
        double seconds = fcurrent / m_rate;
        double ellapsed = m_timer.ellapsed();
        double eta = ellapsed * (m_total / fcurrent - 1);
        double speed = ellapsed ? seconds/ellapsed : 0.0;
        if (m_total == ~0ULL)
            m_disp.put(strutil::format(L"\r%s (%.1fx)   ",
                formatSeconds(seconds).c_str(), speed));
        else
            m_disp.put(strutil::format(L"\r[%.1f%%] %s/%s (%.1fx), ETA %s  ",
                percent, formatSeconds(seconds).c_str(), m_tstamp.c_str(),
                speed, formatSeconds(eta).c_str()));
    }
    void finish(uint64_t current)
    {
        m_disp.flush();
        fputwc('\n', stderr);
        double ellapsed = m_timer.ellapsed();
        fwprintf(stderr, L"%lld/%lld samples processed in %s\n",
                 current, m_total, formatSeconds(ellapsed).c_str());
    }
};

static
void process(const std::shared_ptr<FILE> &ifp,
             const std::shared_ptr<FILE> &ofp, int rate, int quality,
             double bandwidth, int bits)
{
    std::shared_ptr<WaveSource> source(std::make_shared<WaveSource>(ifp));

    const std::vector<uint32_t> *channels = source->getChannels();
    uint32_t chanmask = 0;
    if (channels) {
        for (size_t i = 0; i < channels->size(); ++i)
            chanmask |= (1 << (channels->at(i) - 1));
    }

    std::shared_ptr<IDMODSPEngine> engine =
        std::make_shared<MSResampler>(source, rate, quality, bandwidth);
    std::shared_ptr<ISource> filter =
        std::make_shared<DMODSPProcessor>(source, engine);
    if (bits != 32)
        filter = std::make_shared<Quantizer>(filter, bits, false, bits == 32);

    std::shared_ptr<WaveSink> sink =
        std::make_shared<WaveSink>(ofp.get(), filter->length(),
                                   filter->getSampleFormat(),
                                   chanmask);

    const size_t pull_packets = 4096;
    AudioStreamBasicDescription asbd = filter->getSampleFormat();
    std::vector<uint8_t> buffer(pull_packets * asbd.mBytesPerFrame);

    size_t ns;
    Progress progress(source->length(), source->getSampleFormat().mSampleRate);
    while ((ns = filter->readSamples(&buffer[0], pull_packets)) > 0) {
        sink->writeSamples(&buffer[0], ns * asbd.mBytesPerFrame, ns);
        progress.update(source->getPosition());
    }
    progress.finish(source->getPosition());
}

struct COMInitializer {
    COMInitializer()
    {
        CoInitializeEx(0, COINIT_MULTITHREADED);
    }
    ~COMInitializer()
    {
        CoUninitialize();
    }
};

static void usage()
{
    std::fputws(
L"usage: MSResampler -r RATE [OPTIONS] INFILE OUTFILE\n"
L"\n"
L"\"-\" as INFILE means stdin\n"
L"\"-\" as OUTFILE means stdout\n"
L"[Options]\n"
L"-r <n>     sample rate in Hz (required)\n"
L"-q <n>     quality: 1-60 (default 60)\n"
L"-w <float> lowpass bandwidth: 0.0-1.0 (default 0.95)\n"
L"-b <n>     output bitdepth: 2-32 (default 32)\n"
    , stderr);
    std::exit(1);
}

int wmain(int argc, wchar_t **argv)
{
    _setmode(0, _O_BINARY);
    _setmode(2, _O_U8TEXT);
    std::setbuf(stderr, 0);

    int ch;
    int rate = 0, quality = 60, bits = 32;
    double bandwidth = 0.95;
    while ((ch = getopt::getopt(argc, argv, L"r:q:w:b:")) != -1) {
        switch (ch) {
        case 'r':
            if (std::swscanf(getopt::optarg, L"%d", &rate) != 1 || rate <= 0)
                usage();
            break;
        case 'q':
            if (std::swscanf(getopt::optarg, L"%d", &quality) != 1)
                usage();
            if (quality < 1 || quality > 60)
                usage();
            break;
        case 'w':
            if (std::swscanf(getopt::optarg, L"%lf", &bandwidth) != 1)
                usage();
            if (bandwidth < 0.0 || bandwidth > 1.0)
                usage();
            break;
        case 'b':
            if (std::swscanf(getopt::optarg, L"%d", &bits) != 1)
                usage();
            if (bits < 2 || bits > 32)
                usage();
            break;
        default:
            usage();
        }
    }
    argc -= getopt::optind;
    argv += getopt::optind;
    try {
        if (argc < 2 || !rate)
            usage();
        std::shared_ptr<FILE> ifp = win32::fopen(argv[0], L"rb");
        std::shared_ptr<FILE> ofp = win32::fopen(argv[1], L"wb");
        COMInitializer __com__;
        process(ifp, ofp, rate, quality, bandwidth, bits);
        return 0;
    } catch (const std::exception &e) {
        std::fwprintf(stderr, L"ERROR: %s\n", strutil::us2w(e.what()));
        return 2;
    }
}
