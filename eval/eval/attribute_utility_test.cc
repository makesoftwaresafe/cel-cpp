#include "eval/eval/attribute_utility.h"

#include <vector>

#include "base/attribute_set.h"
#include "common/unknown.h"
#include "common/value.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_value.h"
#include "eval/public/unknown_attribute_set.h"
#include "eval/public/unknown_set.h"
#include "internal/testing.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {

using ::cel::AttributeSet;

using ::cel::UnknownValue;
using ::cel::Value;
using ::testing::Eq;
using ::testing::SizeIs;
using ::testing::UnorderedPointwise;

class AttributeUtilityTest : public ::testing::Test {
 public:
  AttributeUtilityTest() = default;

 protected:
  google::protobuf::Arena arena_;
};

TEST_F(AttributeUtilityTest, UnknownsUtilityCheckUnknowns) {
  std::vector<CelAttributePattern> unknown_patterns = {
      CelAttributePattern("unknown0", {CreateCelAttributeQualifierPattern(
                                          CelValue::CreateInt64(1))}),
      CelAttributePattern("unknown0", {CreateCelAttributeQualifierPattern(
                                          CelValue::CreateInt64(2))}),
      CelAttributePattern("unknown1", {}),
      CelAttributePattern("unknown2", {}),
  };

  std::vector<CelAttributePattern> missing_attribute_patterns;

  AttributeUtility utility(unknown_patterns, missing_attribute_patterns);
  // no match for void trail
  ASSERT_FALSE(utility.CheckForUnknown(AttributeTrail(), true));
  ASSERT_FALSE(utility.CheckForUnknown(AttributeTrail(), false));

  AttributeTrail unknown_trail0("unknown0");

  { ASSERT_FALSE(utility.CheckForUnknown(unknown_trail0, false)); }

  { ASSERT_TRUE(utility.CheckForUnknown(unknown_trail0, true)); }

  {
    ASSERT_TRUE(utility.CheckForUnknown(
        unknown_trail0.Step(
            CreateCelAttributeQualifier(CelValue::CreateInt64(1))),
        false));
  }

  {
    ASSERT_TRUE(utility.CheckForUnknown(
        unknown_trail0.Step(
            CreateCelAttributeQualifier(CelValue::CreateInt64(1))),
        true));
  }
}

TEST_F(AttributeUtilityTest, UnknownsUtilityMergeUnknownsFromValues) {
  std::vector<CelAttributePattern> unknown_patterns;

  std::vector<CelAttributePattern> missing_attribute_patterns;

  CelAttribute attribute0("unknown0", {});
  CelAttribute attribute1("unknown1", {});

  AttributeUtility utility(unknown_patterns, missing_attribute_patterns);

  UnknownValue unknown_set0 =
      cel::UnknownValue(cel::Unknown(AttributeSet({attribute0})));
  UnknownValue unknown_set1 =
      cel::UnknownValue(cel::Unknown(AttributeSet({attribute1})));

  std::vector<cel::Value> values = {
      unknown_set0,
      unknown_set1,
      cel::BoolValue(true),
      cel::IntValue(1),
  };

  absl::optional<UnknownValue> unknown_set = utility.MergeUnknowns(values);
  ASSERT_TRUE(unknown_set.has_value());
  EXPECT_THAT((*unknown_set).attribute_set(),
              UnorderedPointwise(
                  Eq(), std::vector<CelAttribute>{attribute0, attribute1}));
}

TEST_F(AttributeUtilityTest, UnknownsUtilityCheckForUnknownsFromAttributes) {
  std::vector<CelAttributePattern> unknown_patterns = {
      CelAttributePattern("unknown0",
                          {CelAttributeQualifierPattern::CreateWildcard()}),
  };

  std::vector<CelAttributePattern> missing_attribute_patterns;

  AttributeTrail trail0("unknown0");
  AttributeTrail trail1("unknown1");

  CelAttribute attribute1("unknown1", {});
  UnknownSet unknown_set1(UnknownAttributeSet({attribute1}));

  AttributeUtility utility(unknown_patterns, missing_attribute_patterns);

  UnknownSet unknown_attr_set(utility.CheckForUnknowns(
      {
          AttributeTrail(),  // To make sure we handle empty trail gracefully.
          trail0.Step(CreateCelAttributeQualifier(CelValue::CreateInt64(1))),
          trail0.Step(CreateCelAttributeQualifier(CelValue::CreateInt64(2))),
      },
      false));

  UnknownSet unknown_set(unknown_set1, unknown_attr_set);

  ASSERT_THAT(unknown_set.unknown_attributes(), SizeIs(3));
}

TEST_F(AttributeUtilityTest, UnknownsUtilityCheckForMissingAttributes) {
  std::vector<CelAttributePattern> unknown_patterns;

  std::vector<CelAttributePattern> missing_attribute_patterns;

  AttributeTrail trail("destination");
  trail =
      trail.Step(CreateCelAttributeQualifier(CelValue::CreateStringView("ip")));

  AttributeUtility utility0(unknown_patterns, missing_attribute_patterns);
  EXPECT_FALSE(utility0.CheckForMissingAttribute(trail));

  missing_attribute_patterns.push_back(CelAttributePattern(
      "destination",
      {CreateCelAttributeQualifierPattern(CelValue::CreateStringView("ip"))}));

  AttributeUtility utility1(unknown_patterns, missing_attribute_patterns);
  EXPECT_TRUE(utility1.CheckForMissingAttribute(trail));
}

TEST_F(AttributeUtilityTest, CreateUnknownSet) {
  AttributeTrail trail("destination");
  trail =
      trail.Step(CreateCelAttributeQualifier(CelValue::CreateStringView("ip")));

  std::vector<CelAttributePattern> empty_patterns;
  AttributeUtility utility(empty_patterns, empty_patterns);

  UnknownValue set = utility.CreateUnknownSet(trail.attribute());
  ASSERT_THAT(set.attribute_set(), SizeIs(1));
  ASSERT_OK_AND_ASSIGN(auto elem, set.attribute_set().begin()->AsString());
  EXPECT_EQ(elem, "destination.ip");
}

}  // namespace google::api::expr::runtime
