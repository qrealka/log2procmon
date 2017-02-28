// log2pm.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <string>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <locale>
#include <codecvt>
#include <cstdlib> // EXIT_FAILURE

#include <spdlog/spdlog.h>
#include "spdlog/sinks/null_sink.h"

#include <WinIoCtl.h>

// see details: 'Windows Sysinternals Administrator's Reference' by Devid A. Solomon
// https://github.com/Wintellect/ProcMonDebugOutput
#define FILE_DEVICE_PROCMON_LOG 0x00009535
#define IOCTL_EXTERNAL_LOG_DEBUGOUT (ULONG) CTL_CODE( FILE_DEVICE_PROCMON_LOG, 0x81, METHOD_BUFFERED, FILE_WRITE_ACCESS )

template<class Mutex>
class ProcmonSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    ProcmonSink() 
        : spdlog::sinks::base_sink<Mutex>()
        , device(INVALID_HANDLE_VALUE)
    {
        device = ::CreateFileW(L"\\\\.\\Global\\ProcmonDebugLogger",
            GENERIC_WRITE,
            FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    }

    ~ProcmonSink()
    {
        if (device != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(device);
            device = INVALID_HANDLE_VALUE;
        }
    }

private:
    void flush() override
    {
    }

    void _sink_it(const spdlog::details::log_msg& msg) override final
    {
        if (device != INVALID_HANDLE_VALUE)
        {
            std::wstring temp = converter.from_bytes(msg.formatted.c_str());

            DWORD outLength{};

            const auto result = ::DeviceIoControl(device,
                IOCTL_EXTERNAL_LOG_DEBUGOUT, //2503311876 
                &temp[0],
                sizeof(wchar_t) * temp.length(),
                nullptr,
                0,
                &outLength,
                nullptr);

            if (FALSE == result)
            {
                const auto lastError = ::GetLastError();
                ::CloseHandle(device);
                device = INVALID_HANDLE_VALUE;
            }
        }
    }

    HANDLE device;
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
};

using pm_sink = ProcmonSink<spdlog::details::null_mutex>;
using pm_sink_mt = ProcmonSink<std::mutex>;

using namespace std;
using namespace std::chrono;
using namespace spdlog;
using namespace spdlog::sinks;


template<typename T>
inline std::string format(const T& value)
{
    static std::locale loc("");
    std::stringstream ss;
    ss.imbue(loc);
    ss << value;
    return ss.str();
}

template<>
inline std::string format(const double & value)
{
    static std::locale loc("");
    std::stringstream ss;
    ss.imbue(loc);
    ss << std::fixed << std::setprecision(1) << value;
    return ss.str();
}


void bench(int howmany, std::shared_ptr<spdlog::logger> log);
void bench_mt(int howmany, std::shared_ptr<spdlog::logger> log, int thread_count);

int main(int argc, char* argv[])
{
    int queue_size = 1048576;
    int howmany = 10000;
    int threads = 5;

    if (argc > 1)
        howmany = atoi(argv[1]);
    if (argc > 2)
        threads = atoi(argv[2]);
    if (argc > 3)
        queue_size = atoi(argv[3]);

    cout << "*******************************************************************************\n";
    cout << "Single thread, " << format(howmany) << " iterations" << endl;
    cout << "*******************************************************************************\n";
    bench(howmany, spdlog::create<pm_sink>("st"));
    bench(howmany, spdlog::daily_logger_st("daily_st", "logs/daily_st"));
    bench(howmany, spdlog::create<null_sink_st>("null_st"));

    cout << "\n*******************************************************************************\n";
    cout << threads << " threads sharing same logger, " << format(howmany) << " iterations" << endl;
    cout << "*******************************************************************************\n";
    bench_mt(howmany, spdlog::create<pm_sink>("mt"), threads);
    bench_mt(howmany, spdlog::daily_logger_mt("daily_mt", "logs/daily_mt"), threads);
    bench_mt(howmany, spdlog::create<null_sink_mt>("null_mt"), threads);

    cout << "\n*******************************************************************************\n";
    cout << "async logging.. " << threads << " threads sharing same logger, " << format(howmany) << " iterations " << endl;
    cout << "*******************************************************************************\n";

    spdlog::set_async_mode(queue_size);
    for (int i = 0; i < 3; ++i)
    {
        auto as = spdlog::create<pm_sink>("as");
        bench_mt(howmany, as, threads);
        spdlog::drop("as");
    }

    return 0;
}

void bench(int howmany, std::shared_ptr<spdlog::logger> log)
{
    cout << log->name() << "...\t\t" << flush;
    auto start = system_clock::now();
    for (auto i = 0; i < howmany; ++i)
    {
        log->info("Hello logger: msg number {}", i);
    }


    auto delta = system_clock::now() - start;
    auto delta_d = duration_cast<duration<double>> (delta).count();
    cout << format(int(howmany / delta_d)) << "/sec" << endl;
}


void bench_mt(int howmany, std::shared_ptr<spdlog::logger> log, int thread_count)
{

    cout << log->name() << "...\t\t" << flush;
    std::atomic<int > msg_counter{ 0 };
    vector<thread> threads;
    auto start = system_clock::now();
    for (int t = 0; t < thread_count; ++t)
    {
        threads.push_back(std::thread([&]()
        {
            for (;;)
            {
                int counter = ++msg_counter;
                if (counter > howmany) break;
                log->info("Hello logger: msg number {}", counter);
            }
        }));
    }


    for (auto &t : threads)
    {
        if (t.joinable())
            t.join();
    };


    auto delta = system_clock::now() - start;
    auto delta_d = duration_cast<duration<double>> (delta).count();
    cout << format(int(howmany / delta_d)) << "/sec" << endl;
}

