#include <gtest/gtest.h>
#include <syncflow/types.hpp>

namespace {

class VersionVectorTest : public ::testing::Test {
 protected:
  syncflow::VersionVector vv_a_, vv_b_;

  void SetUp() override {
    // VV for device A: [A=3, B=2, C=1]
    vv_a_.update("A", 3);
    vv_a_.update("B", 2);
    vv_a_.update("C", 1);

    // VV for device B: [A=3, B=3, C=1]
    vv_b_.update("A", 3);
    vv_b_.update("B", 3);
    vv_b_.update("C", 1);
  }
};

TEST_F(VersionVectorTest, GetReturnsCorrectClock) {
  EXPECT_EQ(vv_a_.get("A"), 3);
  EXPECT_EQ(vv_a_.get("B"), 2);
  EXPECT_EQ(vv_a_.get("C"), 1);
}

TEST_F(VersionVectorTest, IncrementIncreasesClock) {
  vv_a_.increment("A");
  EXPECT_EQ(vv_a_.get("A"), 4);
}

TEST_F(VersionVectorTest, HappensBeforeDetectsCausality) {
  // vv_a_ = [A=3, B=2, C=1]
  // vv_b_ = [A=3, B=3, C=1]
  // vv_a_ happens_before vv_b_ because B's clock increased
  EXPECT_TRUE(vv_a_.happens_before(vv_b_));
  EXPECT_FALSE(vv_b_.happens_before(vv_a_));
}

TEST_F(VersionVectorTest, ConcurrentDetectsConcurrency) {
  // Create conflicting vectors
  syncflow::VersionVector vv1, vv2;
  vv1.update("A", 2);
  vv1.update("B", 1);
  
  vv2.update("A", 1);
  vv2.update("B", 2);
  
  // Neither happens before the other → concurrent
  EXPECT_TRUE(vv1.concurrent_with(vv2));
  EXPECT_TRUE(vv2.concurrent_with(vv1));
}

TEST_F(VersionVectorTest, EqualityWorks) {
  syncflow::VersionVector vv_copy;
  vv_copy.update("A", 3);
  vv_copy.update("B", 2);
  vv_copy.update("C", 1);
  
  EXPECT_TRUE(vv_a_.equal_to(vv_copy));
  EXPECT_FALSE(vv_a_.equal_to(vv_b_));
}

TEST_F(VersionVectorTest, ToStringProducesValidJSON) {
  std::string json_str = vv_a_.to_string();
  EXPECT_NE(json_str.find("A"), std::string::npos);
  EXPECT_NE(json_str.find("3"), std::string::npos);
}

}  // anonymous namespace
