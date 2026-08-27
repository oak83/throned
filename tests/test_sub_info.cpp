#include "include/configs/sub/SubInfo.h"

#include <QTest>

using Configs::ParseSubInfo;

class TestSubInfo : public QObject {
    Q_OBJECT

private slots:
    void readsTheWholeHeader();
    void absentTotalMeansNothingToShow();
    void zeroTotalIsUnlimitedNotEmpty();
    void usageIsClampedToItsAllowance();
    void daysLeftCountsWholeDays();
};

void TestSubInfo::readsTheWholeHeader() {
    const auto sub = ParseSubInfo("upload=100; download=200; total=1000; expire=1948602000");
    QVERIFY(sub.valid);
    QCOMPARE(sub.upload, 100);
    QCOMPARE(sub.download, 200);
    QCOMPARE(sub.used(), 300);
    QCOMPARE(sub.total, 1000);
    QCOMPARE(sub.expire, 1948602000);
}

// Without a total there is no allowance to draw, so the tab must stay bare.
void TestSubInfo::absentTotalMeansNothingToShow() {
    QVERIFY(!ParseSubInfo("upload=100; download=200").valid);
    QVERIFY(!ParseSubInfo("").valid);
    QVERIFY(!ParseSubInfo("   ").valid);
}

// "total=0" is the panels' way of saying unlimited, which is not the same as a missing field.
void TestSubInfo::zeroTotalIsUnlimitedNotEmpty() {
    const auto sub = ParseSubInfo("upload=1; download=2; total=0");
    QVERIFY(sub.valid);
    QCOMPARE(sub.total, 0);
    QCOMPARE(sub.usedFraction(), -1.0);
}

void TestSubInfo::usageIsClampedToItsAllowance() {
    QCOMPARE(ParseSubInfo("upload=500; download=0; total=1000").usedFraction(), 0.5);
    // Providers do let usage run past the plan; the line must not overflow its track.
    QCOMPARE(ParseSubInfo("upload=1500; download=0; total=1000").usedFraction(), 1.0);
}

void TestSubInfo::daysLeftCountsWholeDays() {
    Configs::SubInfo sub;
    sub.expire = 1000000;
    QCOMPARE(sub.daysLeft(1000000 - 86400 * 3), 3);
    QCOMPARE(sub.daysLeft(1000000 - 100), 0);
    QCOMPARE(sub.daysLeft(1000000 + 86400), 0);
    QCOMPARE(Configs::SubInfo{}.daysLeft(1000000), -1);
}

QTEST_GUILESS_MAIN(TestSubInfo)
#include "test_sub_info.moc"
