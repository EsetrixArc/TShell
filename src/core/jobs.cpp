// jobs.cpp — Job control and redirect application
#include "globals.hpp"
#include "jobs.hpp"
#include "debug.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>

// =============================================================================
//  Job control
// =============================================================================

void printJob(const Job& j) {
    const char* s = j.status == Job::Status::Running ? "Running"
                  : j.status == Job::Status::Stopped ? "Stopped" : "Done";
    std::cout << "[" << j.id << "] " << s << "\t" << j.cmdline << "\n";
}

Job* findJob(int id) {
    for (auto& j : g_jobs) if (j.id == id) return &j;
    return nullptr;
}

void reapJobs() {
    for (auto& j : g_jobs) {
        if (j.status == Job::Status::Done) continue;
        int st;
        pid_t r = waitpid(-j.pgid, &st, WNOHANG | WUNTRACED);
        if (r > 0) {
            if (WIFSTOPPED(st))  j.status = Job::Status::Stopped;
            else                 j.status = Job::Status::Done;
        }
    }
    for (auto& j : g_jobs)
        if (j.status == Job::Status::Done) printJob(j);
    g_jobs.erase(
        std::remove_if(g_jobs.begin(), g_jobs.end(),
                       [](const Job& j){ return j.status == Job::Status::Done; }),
        g_jobs.end());
}

int waitFg(Job& j) {
    int status = 0;
    for (size_t i = 0; i < j.pids.size(); ++i) {
        int s;
        while (waitpid(j.pids[i], &s, WUNTRACED) < 0 && errno == EINTR);
        if (WIFSTOPPED(s)) {
            j.status = Job::Status::Stopped;
            std::cout << "\n[" << j.id << "] Stopped\t" << j.cmdline << "\n";
            return 128 + SIGTSTP;
        }
        int ec = WIFEXITED(s) ? WEXITSTATUS(s) : 128 + WTERMSIG(s);
        if (i < j.exitCodes.size()) j.exitCodes[i] = ec;
        if (i == j.pids.size() - 1) status = ec;
    }
    j.status = Job::Status::Done;
    return status;
}

// =============================================================================
//  Redirect application (runs in child process after fork)
// =============================================================================

void applyRedirects(const std::vector<Redirect>& reds) {
    for (auto& r : reds) {
        int fd = -1;
        switch (r.type) {

        case Redirect::Type::In:
            if (r.target.rfind("heredoc:", 0) == 0) {
                std::string content = r.target.substr(8);
                int pfd[2];
                if (pipe(pfd) < 0) { perror("pipe"); _exit(1); }
                if (fork() == 0) {
                    close(pfd[0]);
                    ssize_t _w = write(pfd[1], content.c_str(), content.size());
                    (void)_w;
                    _exit(0);
                }
                close(pfd[1]);
                dup2(pfd[0], STDIN_FILENO);
                close(pfd[0]);
            } else {
                fd = open(r.target.c_str(), O_RDONLY);
                if (fd < 0) { perror(r.target.c_str()); _exit(1); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            break;

        case Redirect::Type::ProcSubIn:
            fd = open(r.target.c_str(), O_RDONLY);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDIN_FILENO); close(fd);
            break;

        case Redirect::Type::ProcSubOut:
            fd = open(r.target.c_str(), O_WRONLY);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
            break;

        case Redirect::Type::Out:
            fd = open(r.target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
            break;

        case Redirect::Type::OutAppend:
            fd = open(r.target.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
            break;

        case Redirect::Type::Err:
            fd = open(r.target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDERR_FILENO); close(fd);
            break;

        case Redirect::Type::ErrAppend:
            fd = open(r.target.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDERR_FILENO); close(fd);
            break;

        case Redirect::Type::Both:
            fd = open(r.target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd);
            break;

        case Redirect::Type::BothAppend:
            fd = open(r.target.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(r.target.c_str()); _exit(1); }
            dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd);
            break;

        case Redirect::Type::HereStr:
            break; // already converted to heredoc: prefix
        }
    }
}
