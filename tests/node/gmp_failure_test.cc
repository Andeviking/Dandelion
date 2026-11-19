#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>

#include "test_support.h"

namespace dandelion::testing {

TEST(GmpFailure, LimbAllocationFailureTerminatesAnIsolatedProcess) {
  const auto child = ::fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    ::execl(DANDELION_GMP_OOM_PROBE_PATH, DANDELION_GMP_OOM_PROBE_PATH,
            static_cast<char*>(nullptr));
    ::_exit(127);
  }
  int status = 0;
  pid_t waited;
  do {
    waited = ::waitpid(child, &status, 0);
  } while (waited == -1 && errno == EINTR);
  ASSERT_EQ(waited, child);
  ASSERT_TRUE(WIFSIGNALED(status)) << status;
  EXPECT_EQ(WTERMSIG(status), SIGABRT);
}

}  // namespace dandelion::testing
