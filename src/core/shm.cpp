#include "infsched/shm.hpp"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace infsched {

SharedMemoryBuffer::SharedMemoryBuffer(int fd, void* addr, size_t size, bool ownsFd)
    : fd_(fd), addr_(addr), size_(size), ownsFd_(ownsFd) {}

SharedMemoryBuffer::~SharedMemoryBuffer() {
  if (addr_ != nullptr && addr_ != MAP_FAILED) {
    munmap(addr_, size_);
  }
  if (ownsFd_ && fd_ >= 0) {
    close(fd_);
  }
}

std::shared_ptr<SharedMemoryBuffer> SharedMemoryBuffer::create(const std::string& name, size_t sizeBytes) {
  // Use memfd_create
  int fd = static_cast<int>(syscall(SYS_memfd_create, name.c_str(), MFD_CLOEXEC));
  if (fd < 0) {
    // Fallback: temp file in /dev/shm
    std::string path = "/dev/shm/" + name + "_" + std::to_string(getpid()) + "_" + std::to_string(reinterpret_cast<uintptr_t>(&name));
    fd = open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      unlink(path.c_str());
    }
  }

  if (fd < 0) return nullptr;

  if (ftruncate(fd, static_cast<off_t>(sizeBytes)) != 0) {
    close(fd);
    return nullptr;
  }

  void* addr = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    close(fd);
    return nullptr;
  }

  return std::shared_ptr<SharedMemoryBuffer>(new SharedMemoryBuffer(fd, addr, sizeBytes, true));
}

std::shared_ptr<SharedMemoryBuffer> SharedMemoryBuffer::fromFd(int fd, size_t sizeBytes) {
  if (fd < 0) return nullptr;

  void* addr = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    return nullptr;
  }

  return std::shared_ptr<SharedMemoryBuffer>(new SharedMemoryBuffer(fd, addr, sizeBytes, false));
}

} // namespace infsched
