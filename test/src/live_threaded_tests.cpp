#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>  // this_thread

#include "databento/constants.hpp"
#include "databento/datetime.hpp"
#include "databento/dbn.hpp"
#include "databento/enums.hpp"
#include "databento/live_threaded.hpp"
#include "databento/record.hpp"
#include "mock/mock_lsg_server.hpp"

namespace databento {
namespace test {
class LiveThreadedTests : public testing::Test {
 protected:
  static constexpr auto kKey = "32-character-with-lots-of-filler";
};

TEST_F(LiveThreadedTests, TestBasic) {
  const MboMsg kRec{{sizeof(MboMsg) / 4, RType::Mbo, 1, 2, UnixNanos{}},
                    1,
                    2,
                    3,
                    0,
                    4,
                    Action::Add,
                    Side::Bid,
                    UnixNanos{},
                    TimeDeltaNanos{},
                    100};
  const mock::MockLsgServer mock_server{dataset::kGlbxMdp3,
                                        [&kRec](mock::MockLsgServer& self) {
                                          self.Accept();
                                          self.Authenticate();
                                          self.Start(Schema::Mbo);
                                          self.SendRecord(kRec);
                                          self.SendRecord(kRec);
                                        }};

  LiveThreaded target{kKey, dataset::kGlbxMdp3, "127.0.0.1", mock_server.Port(),
                      false};
  std::atomic<std::uint32_t> call_count{};
  target.Start([&call_count, &kRec](const Record& rec) {
    ++call_count;
    ASSERT_TRUE(rec.Holds<MboMsg>());
    EXPECT_EQ(rec.Get<MboMsg>(), kRec);
  });
  while (call_count < 2) {
    std::this_thread::yield();
  }
}

TEST_F(LiveThreadedTests, TestTimeoutRecovery) {
  constexpr auto kSchema = Schema::Ohlcv1M;
  const MboMsg kRec{{sizeof(MboMsg) / 4, RType::Mbo, 1, 2, UnixNanos{}},
                    1,
                    2,
                    3,
                    0,
                    4,
                    Action::Add,
                    Side::Bid,
                    UnixNanos{},
                    TimeDeltaNanos{},
                    100};
  std::atomic<std::uint32_t> call_count{};
  const mock::MockLsgServer mock_server{
      dataset::kXnasItch, [&kRec, &call_count](mock::MockLsgServer& self) {
        self.Accept();
        self.Authenticate();
        self.Start(kSchema);
        self.SendRecord(kRec);
        while (call_count < 1) {
          std::this_thread::yield();
        }
        // 150% of live threaded timeout
        std::this_thread::sleep_for(std::chrono::milliseconds{75});
        self.SendRecord(kRec);
      }};

  LiveThreaded target{kKey, dataset::kXnasItch, "127.0.0.1", mock_server.Port(),
                      false};
  const Metadata metadata =
      target.Start([&call_count, &kRec](const Record& rec) {
        ++call_count;
        ASSERT_TRUE(rec.Holds<MboMsg>());
        EXPECT_EQ(rec.Get<MboMsg>(), kRec);
      });
  EXPECT_EQ(metadata.schema, kSchema);
  while (call_count < 2) {
    std::this_thread::yield();
  }
}
}  // namespace test
}  // namespace databento