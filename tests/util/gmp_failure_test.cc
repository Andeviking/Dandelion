#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

#include <gtest/gtest.h>

namespace dandelion::testing::util {
namespace {

int RunProbe(const char* executable, const char* argument) {
  const auto child = ::fork();
  if (child == -1) {
    ADD_FAILURE() << "fork failed with errno " << errno;
    return -1;
  }
  if (child == 0) {
    ::execl(executable, executable, argument, static_cast<char*>(nullptr));
    ::_exit(127);
  }

  int status = 0;
  pid_t waited;
  do {
    waited = ::waitpid(child, &status, 0);
  } while (waited == -1 && errno == EINTR);
  if (waited != child) {
    ADD_FAILURE() << "waitpid failed with errno " << errno;
    return -1;
  }
  return status;
}

TEST(UtilNumericBoundary, PrimaryModuleLinksWithoutNodeFrontendOrGtest) {
  const auto status = RunProbe(DANDELION_UTIL_IMPORT_PROBE_PATH, nullptr);
  ASSERT_NE(status, -1);
  ASSERT_TRUE(WIFEXITED(status)) << "wait status " << status;
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(UtilGmpFailure, LinkedRuntimeVersionAndExactArithmetic) {
  const auto status = RunProbe(DANDELION_UTIL_GMP_OOM_PROBE_PATH, "runtime");
  ASSERT_NE(status, -1);
  ASSERT_TRUE(WIFEXITED(status)) << "wait status " << status;
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(UtilGmpFailure, DefaultLimbAllocatorAbortsOnAddressSpaceLimit) {
  const auto status = RunProbe(DANDELION_UTIL_GMP_OOM_PROBE_PATH, "oom");
  ASSERT_NE(status, -1);
  if (WIFEXITED(status)) {
    FAIL() << "GMP OOM probe exited with code " << WEXITSTATUS(status)
           << " before the expected SIGABRT";
  }
  ASSERT_TRUE(WIFSIGNALED(status)) << "wait status " << status;
  EXPECT_EQ(WTERMSIG(status), SIGABRT);
}

}  // namespace
}  // namespace dandelion::testing::util
