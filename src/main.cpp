#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace fs = std::filesystem;

struct ProcessResult { DWORD exitCode = 1; std::string output; };
struct Video {
    std::string id, title;
    double percent = 0.0;
    std::string speed = "-", eta = "-", status = "Queued";
};

static std::mutex g_mutex;
static std::vector<Video> g_videos;
static std::atomic_size_t g_next{0};
static std::atomic_int g_finished{0};
static std::string g_playlistName;
static fs::path g_outputDir;
static int g_workerCount = 5;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    out.resize(static_cast<size_t>(n - 1));
    return out;
}

std::wstring quoteArg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    unsigned backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') ++backslashes;
        else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring makeCommandLine(const std::vector<std::wstring>& args) {
    std::wstring cmd;
    for (const auto& a : args) {
        if (!cmd.empty()) cmd.push_back(L' ');
        cmd += quoteArg(a);
    }
    return cmd;
}

ProcessResult runProcess(
    const std::vector<std::wstring>& args,
    const std::function<void(const std::string&)>& onLine = {}) {

    ProcessResult result;
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.output = "Could not create process output pipe.";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring command = makeCommandLine(args);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        result.output = "Could not start yt-dlp.exe. Is it installed and available in PATH?";
        return result;
    }

    std::string pending;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        result.output.append(buffer, bytesRead);
        pending.append(buffer, bytesRead);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
            pending.erase(0, pos + 1);
        }
    }
    if (!pending.empty() && onLine) onLine(pending);
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &result.exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return result;
}

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string sanitizeFolderName(std::string name) {
    name = trim(name);
    const std::string invalid = "<>:\"/\\|?*";
    for (char& c : name) {
        if (static_cast<unsigned char>(c) < 32 || invalid.find(c) != std::string::npos) c = '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (name.empty()) name = "YouTube Playlist";
    if (name.size() > 120) name.resize(120);
    return name;
}

fs::path getDownloadsFolder() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &raw)) && raw) {
        fs::path p(raw);
        CoTaskMemFree(raw);
        return p;
    }
    wchar_t* profile = _wgetenv(L"USERPROFILE");
    return profile ? fs::path(profile) / L"Downloads" : fs::current_path() / L"Downloads";
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t p = line.find('\t', start);
        if (p == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, p - start));
        start = p + 1;
    }
    return parts;
}

std::string urlForId(const std::string& id) {
    return "https://www.youtube.com/watch?v=" + id;
}

std::string formatBytes(double bytes) {
    if (bytes < 0 || !std::isfinite(bytes)) return "-";
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        ++u;
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(u == 0 ? 0 : 1) << bytes << ' ' << units[u];
    return os.str();
}

void updateProgress(size_t index, const std::string& line) {
    if (line.rfind("DLPROG|", 0) != 0) return;

    std::vector<std::string> v;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, '|')) v.push_back(item);
    if (v.size() < 6) return;

    try {
        double downloaded = std::stod(v[1]);
        double total = std::stod(v[2]);
        if (total <= 0) total = std::stod(v[3]);
        double pct = total > 0
            ? std::clamp(downloaded * 100.0 / total, 0.0, 100.0)
            : 0.0;
        std::string speed = v[4] == "NA" ? "-" : formatBytes(std::stod(v[4])) + "/s";
        std::string eta = v[5] == "NA" ? "-" : v[5] + "s";

        std::lock_guard<std::mutex> lock(g_mutex);
        g_videos[index].percent = pct;
        g_videos[index].speed = speed;
        g_videos[index].eta = eta;
        g_videos[index].status = "Downloading";
    } catch (...) {}
}

void setStatus(size_t index, const std::string& status) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_videos[index].status = status;
}

void drawBar(double pct, int width = 28) {
    int filled = static_cast<int>(std::round(std::clamp(pct, 0.0, 100.0) * width / 100.0));
    std::cout << '[';
    for (int i = 0; i < width; ++i) std::cout << (i < filled ? '#' : '-');
    std::cout << ']';
}

void renderDashboard() {
    std::lock_guard<std::mutex> lock(g_mutex);
    int completed = g_finished.load();
    double overall = 0.0;
    for (const auto& v : g_videos)
        overall += (v.status == "Completed" ? 100.0 : v.percent);
    if (!g_videos.empty()) overall /= g_videos.size();

    std::cout << "\x1b[2J\x1b[H";
    std::cout << "\x1b[96mPLAYLISTDL\x1b[0m  |  YouTube playlist downloader\n";
    std::cout << "Playlist: " << g_playlistName << "\n";
    std::cout << "Output:   " << narrow(g_outputDir.wstring()) << "\n\n";
    std::cout << "Videos: " << g_videos.size()
              << "   Completed: " << completed
              << "   Failed: " << std::count_if(
                    g_videos.begin(), g_videos.end(),
                    [](const Video& v) { return v.status == "Failed"; })
              << "   Workers: " << g_workerCount << "\n";

    drawBar(overall, 48);
    std::cout << "  " << std::fixed << std::setprecision(1) << overall << "% overall\n\n";
    std::cout << "ACTIVE / QUEUED DOWNLOADS\n";
    std::cout << "---------------------------------------------------------------\n";

    int shown = 0;
    for (const auto& v : g_videos) {
        if (v.status == "Completed") continue;
        std::string title = v.title;
        if (title.size() > 39) title = title.substr(0, 36) + "...";

        std::cout << std::setw(3) << (++shown) << ". "
                  << std::left << std::setw(40) << title
                  << std::right << " " << std::setw(10) << v.status << "\n     ";
        drawBar(v.percent, 28);
        std::cout << " " << std::setw(5) << std::setprecision(1) << v.percent
                  << "%  " << std::setw(10) << v.speed << "  ETA " << v.eta << "\n";
        if (shown >= 12) {
            std::cout << "... and " << (g_videos.size() - shown) << " more videos\n";
            break;
        }
    }

    if (shown == 0) std::cout << "All videos completed.\n";
    std::cout << "\nPress Ctrl+C to stop. Completed files remain in the playlist folder.\n";
    std::cout.flush();
}

void downloadOne(size_t index) {
    Video video;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        video = g_videos[index];
        g_videos[index].status = "Starting";
    }

    std::ostringstream prefix;
    prefix << std::setw(3) << std::setfill('0') << (index + 1) << " - ";
    fs::path outputTemplate = g_outputDir / (prefix.str() + "%(title)s.%(ext)s");

    const std::string progressTemplate =
        "DLPROG|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|"
        "%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s";

    std::vector<std::wstring> args = {
        L"yt-dlp.exe", L"--no-warnings", L"--newline", L"--no-playlist",
        L"--no-overwrites", L"--continue", L"--windows-filenames",
        L"--trim-filenames", L"180", L"--progress",
        L"--progress-template", widen(progressTemplate),
        L"-f", L"bv*+ba/b", L"-o", outputTemplate.wstring(),
        widen(urlForId(video.id))
    };

    ProcessResult r = runProcess(args, [index](const std::string& line) {
        updateProgress(index, line);
    });

    if (r.exitCode == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_videos[index].percent = 100.0;
        g_videos[index].status = "Completed";
    } else {
        setStatus(index, "Failed");
    }
    ++g_finished;
}

void worker() {
    while (true) {
        size_t index = g_next.fetch_add(1);
        if (index >= g_videos.size()) return;
        downloadOne(index);
    }
}

bool enableAnsi() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return false;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    enableAnsi();

    std::cout << "=============================================\n";
    std::cout << "             PLAYLISTDL v1.0\n";
    std::cout << "=============================================\n\n";
    std::cout << "Paste a public YouTube video or playlist URL:\n> ";

    std::string playlistUrl;
    std::getline(std::cin, playlistUrl);
    playlistUrl = trim(playlistUrl);
    if (playlistUrl.empty()) {
        std::cerr << "No URL entered.\n";
        return 1;
    }

    std::cout << "\nHow many simultaneous downloads? (1-20, default 5): ";
    std::string workerInput;
    std::getline(std::cin, workerInput);
    if (!trim(workerInput).empty()) {
        try {
            int n = std::stoi(workerInput);
            if (n < 1 || n > 20) throw std::out_of_range("workers");
            g_workerCount = n;
        } catch (...) {
            std::cout << "Invalid value; using 5 workers.\n";
            g_workerCount = 5;
        }
    }

    std::cout << "\nReading playlist information...\n";
    const std::string metadataTemplate = "%(playlist_title)s\t%(title)s\t%(id)s";
    ProcessResult metadata = runProcess({
        L"yt-dlp.exe", L"--flat-playlist", L"--ignore-errors",
        L"--no-warnings", L"--skip-download", L"--print",
        widen(metadataTemplate), widen(playlistUrl)
    });

    std::istringstream input(metadata.output);
    std::string line;
    std::vector<Video> parsed;
    std::string playlistTitle;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fields = splitTabs(line);
        if (fields.size() < 3) continue;

        std::string ptitle = trim(fields[0]);
        std::string title = trim(fields[1]);
        std::string id = trim(fields[2]);
        if (id.empty() || id == "NA" || title.empty()) continue;
        if (playlistTitle.empty() && !ptitle.empty() && ptitle != "NA")
            playlistTitle = ptitle;
        parsed.push_back(Video{id, title});
    }

    if (parsed.empty()) {
        std::cerr << "\nCould not read any videos from that URL.\n";
        std::cerr << "Check the URL, update yt-dlp, and ensure the playlist is accessible.\n";
        if (!metadata.output.empty()) std::cerr << "\n" << metadata.output << "\n";
        return 2;
    }

    g_playlistName = sanitizeFolderName(
        playlistTitle.empty() ? "YouTube Playlist" : playlistTitle);
    g_outputDir = getDownloadsFolder() / widen(g_playlistName);

    try {
        fs::create_directories(g_outputDir);
    } catch (const std::exception& e) {
        std::cerr << "Could not create output folder: " << e.what() << "\n";
        return 3;
    }

    g_videos = std::move(parsed);
    std::cout << "\nFound " << g_videos.size() << " videos.\n";
    std::cout << "Saving to: " << narrow(g_outputDir.wstring()) << "\n";
    std::cout << "Starting " << g_workerCount << " workers...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::vector<std::thread> workers;
    int workerCount = std::min<int>(g_workerCount, static_cast<int>(g_videos.size()));
    for (int i = 0; i < workerCount; ++i) workers.emplace_back(worker);

    bool done = false;
    while (!done) {
        renderDashboard();
        done = g_finished.load() >= static_cast<int>(g_videos.size());
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    for (auto& t : workers) if (t.joinable()) t.join();
    renderDashboard();

    int failed = 0;
    for (const auto& v : g_videos) if (v.status == "Failed") ++failed;
    std::cout << "\nFinished. " << (g_videos.size() - failed) << " succeeded, "
              << failed << " failed.\n";
    std::cout << "Files saved in: " << narrow(g_outputDir.wstring()) << "\n";
    std::cout << "Press Enter to exit...";
    std::string pause;
    std::getline(std::cin, pause);
    return failed == 0 ? 0 : 4;
}
