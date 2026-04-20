// Copyright (c) 2012 - 2017 Object Computing, Inc.
// All rights reserved.
// See the file license.txt for licensing information.

#define BOOST_TEST_NO_MAIN LiquibookTest
#include <boost/test/unit_test.hpp>

#include "ut_utils.h"
#include "changed_checker.h"

namespace liquibook {

using simple::SimpleOrder;
typedef FillCheck<SimpleOrder*> SimpleFillCheck;
typedef test::ChangedChecker<5> ChangedChecker;

// ---------------------------------------------------------------------------
// Section 1: Depth visibility — iceberg tip only
//   The defining characteristic of iceberg orders: the depth (order book)
//   should only reflect the visible "tip" quantity, not the total.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidDepthShowsTipOnly)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);

  // Order reporting: pre-add
  BOOST_CHECK_EQUAL(true, iceberg_bid.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg_bid.visible_qty());
  BOOST_CHECK_EQUAL(1000u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(1000u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.filled_cost());

  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Order reporting: post-add
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(1000u, iceberg_bid.open_qty());

  // Depth should show only the visible tip (100), not the total (1000)
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestIcebergAskDepthShowsTipOnly)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 500, 0, book::oc_no_conditions, 50);

  BOOST_CHECK_EQUAL(true, iceberg_ask.is_iceberg());
  BOOST_CHECK_EQUAL(50u, iceberg_ask.visible_qty());
  BOOST_CHECK_EQUAL(500u, iceberg_ask.order_qty());

  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 1, 50));
}

BOOST_AUTO_TEST_CASE(TestIcebergBidMixedWithRegularDepth)
{
  SimpleOrderBook order_book;
  SimpleOrder regular_bid(true, 1250, 200);
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);

  BOOST_CHECK_EQUAL(false, regular_bid.is_iceberg());
  BOOST_CHECK_EQUAL(0u, regular_bid.visible_qty());
  BOOST_CHECK_EQUAL(true, iceberg_bid.is_iceberg());

  BOOST_CHECK(add_and_verify(order_book, &regular_bid, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  BOOST_CHECK_EQUAL(simple::os_accepted, regular_bid.state());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());

  // Depth at 1250: regular 200 + iceberg tip 100 = 300 total visible, 2 orders
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 2, 300));
}

BOOST_AUTO_TEST_CASE(TestIcebergAskMixedWithRegularDepth)
{
  SimpleOrderBook order_book;
  SimpleOrder regular_ask(false, 1250, 300);
  SimpleOrder iceberg_ask(false, 1250, 800, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &regular_ask, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  BOOST_CHECK_EQUAL(simple::os_accepted, regular_ask.state());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(800u, iceberg_ask.open_qty());

  // 300 + 100 visible = 400 aggregate visible
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 2, 400));
}

BOOST_AUTO_TEST_CASE(TestMultipleIcebergsSamePriceLevel)
{
  SimpleOrderBook order_book;
  SimpleOrder ice1(true, 1250, 1000, 0, book::oc_no_conditions, 100);
  SimpleOrder ice2(true, 1250, 500, 0, book::oc_no_conditions, 50);

  BOOST_CHECK_EQUAL(true, ice1.is_iceberg());
  BOOST_CHECK_EQUAL(true, ice2.is_iceberg());

  BOOST_CHECK(add_and_verify(order_book, &ice1, false));
  BOOST_CHECK(add_and_verify(order_book, &ice2, false));

  BOOST_CHECK_EQUAL(simple::os_accepted, ice1.state());
  BOOST_CHECK_EQUAL(simple::os_accepted, ice2.state());
  BOOST_CHECK_EQUAL(1000u, ice1.open_qty());
  BOOST_CHECK_EQUAL(500u, ice2.open_qty());

  // Two icebergs: 100 + 50 = 150 visible
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 2, 150));
}

// ---------------------------------------------------------------------------
// Section 2: Tip replenishment after partial fills
//   When the visible portion of an iceberg is filled, a new tip should be
//   carved from the hidden quantity and posted to the book.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidReplenishesAfterFill)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);
  SimpleOrder sell(false, 1250, 100);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Sell 100 — fills the visible tip completely
  {
    SimpleFillCheck fc_sell(&sell, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Order reporting after one tip fill
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(1000u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(100u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(900u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 100u), iceberg_bid.filled_cost());
  BOOST_CHECK_EQUAL(true, iceberg_bid.is_iceberg());

  // Depth should still show 100 at 1250 (replenished tip)
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));

  // Sell reporting
  BOOST_CHECK_EQUAL(simple::os_complete, sell.state());
  BOOST_CHECK_EQUAL(100u, sell.filled_qty());
}

BOOST_AUTO_TEST_CASE(TestIcebergAskReplenishesAfterFill)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 500, 0, book::oc_no_conditions, 50);
  SimpleOrder buy(true, 1250, 50);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  {
    SimpleFillCheck fc_buy(&buy, 50, 1250 * 50);
    SimpleFillCheck fc_ice(&iceberg_ask, 50, 1250 * 50);
    BOOST_CHECK(add_and_verify(order_book, &buy, true, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(500u, iceberg_ask.order_qty());
  BOOST_CHECK_EQUAL(50u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(450u, iceberg_ask.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 50u), iceberg_ask.filled_cost());

  // Replenished: still 50 visible from remaining 450
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 1, 50));

  BOOST_CHECK_EQUAL(simple::os_complete, buy.state());
}

BOOST_AUTO_TEST_CASE(TestIcebergBidPartialTipFillNoReplenish)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 500, 0, book::oc_no_conditions, 100);
  SimpleOrder sell(false, 1250, 60);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Sell only 60 — partial fill of the tip
  {
    SimpleFillCheck fc_sell(&sell, 60, 1250 * 60);
    SimpleFillCheck fc_ice(&iceberg_bid, 60, 1250 * 60);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(500u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(60u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(440u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 60u), iceberg_bid.filled_cost());

  // Tip not fully consumed — no replenish, show remaining tip = 40
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 40));
}

BOOST_AUTO_TEST_CASE(TestIcebergLastSliceSmallerThanTip)
{
  SimpleOrderBook order_book;
  // total=250, tip=100 -> slices: 100, 100, 50
  SimpleOrder iceberg_bid(true, 1250, 250, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Fill first tip
  SimpleOrder sell1(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell1, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell1, true, true));
  }
  BOOST_CHECK_EQUAL(100u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(150u, iceberg_bid.open_qty());
  DepthCheck<SimpleOrderBook> dc1(order_book.depth());
  BOOST_CHECK(dc1.verify_bid(1250, 1, 100));

  // Fill second tip
  SimpleOrder sell2(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell2, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell2, true, true));
  }
  BOOST_CHECK_EQUAL(200u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(50u, iceberg_bid.open_qty());
  // Third (last) tip: only 50 remains
  DepthCheck<SimpleOrderBook> dc2(order_book.depth());
  BOOST_CHECK(dc2.verify_bid(1250, 1, 50));

  // Fill final slice
  SimpleOrder sell3(false, 1250, 50);
  {
    SimpleFillCheck fc_sell(&sell3, 50, 1250 * 50);
    SimpleFillCheck fc_ice(&iceberg_bid, 50, 1250 * 50);
    BOOST_CHECK(add_and_verify(order_book, &sell3, true, true));
  }
  // Order reporting: fully consumed
  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_bid.state());
  BOOST_CHECK_EQUAL(250u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(250u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 250u), iceberg_bid.filled_cost());
}

// ---------------------------------------------------------------------------
// Section 3: Complete drain across multiple fills
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidFullDrainMultipleFills)
{
  SimpleOrderBook order_book;
  // total=500, tip=100 -> 5 slices
  SimpleOrder iceberg_bid(true, 1250, 500, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  book::Cost running_cost = 0;
  for (int i = 0; i < 5; ++i) {
    SimpleOrder sell(false, 1250, 100);
    {
      SimpleFillCheck fc_sell(&sell, 100, 1250 * 100);
      SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
      BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
    }
    running_cost += 1250 * 100;
    BOOST_CHECK_EQUAL((book::Quantity)((i + 1) * 100), iceberg_bid.filled_qty());
    BOOST_CHECK_EQUAL(running_cost, iceberg_bid.filled_cost());
  }

  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_bid.state());
  BOOST_CHECK_EQUAL(500u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 500u), iceberg_bid.filled_cost());
  BOOST_CHECK_EQUAL(0u, order_book.bids().size());
}

BOOST_AUTO_TEST_CASE(TestIcebergAskFullDrainMultipleFills)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 300, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  for (int i = 0; i < 3; ++i) {
    SimpleOrder buy(true, 1250, 100);
    {
      SimpleFillCheck fc_buy(&buy, 100, 1250 * 100);
      SimpleFillCheck fc_ice(&iceberg_ask, 100, 1250 * 100);
      BOOST_CHECK(add_and_verify(order_book, &buy, true, true));
    }
    BOOST_CHECK_EQUAL((book::Quantity)((i + 1) * 100), iceberg_ask.filled_qty());
  }

  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_ask.state());
  BOOST_CHECK_EQUAL(300u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_ask.open_qty());
  BOOST_CHECK_EQUAL(0u, order_book.asks().size());
}

// ---------------------------------------------------------------------------
// Section 4: Large incoming order consuming entire iceberg in one match
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidFilledByLargeSell)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 500, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  SimpleOrder large_sell(false, 1250, 500);
  {
    SimpleFillCheck fc_sell(&large_sell, 500, 1250 * 500);
    SimpleFillCheck fc_ice(&iceberg_bid, 500, 1250 * 500);
    BOOST_CHECK(add_and_verify(order_book, &large_sell, true, true));
  }

  // Order reporting: both complete
  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_bid.state());
  BOOST_CHECK_EQUAL(500u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL(simple::os_complete, large_sell.state());
  BOOST_CHECK_EQUAL(500u, large_sell.filled_qty());
  BOOST_CHECK_EQUAL(0u, order_book.bids().size());
}

BOOST_AUTO_TEST_CASE(TestIcebergAskFilledByLargeBuy)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 800, 0, book::oc_no_conditions, 200);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  SimpleOrder large_buy(true, 1250, 800);
  {
    SimpleFillCheck fc_buy(&large_buy, 800, 1250 * 800);
    SimpleFillCheck fc_ice(&iceberg_ask, 800, 1250 * 800);
    BOOST_CHECK(add_and_verify(order_book, &large_buy, true, true));
  }

  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_ask.state());
  BOOST_CHECK_EQUAL(800u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_ask.open_qty());
  BOOST_CHECK_EQUAL(simple::os_complete, large_buy.state());
  BOOST_CHECK_EQUAL(0u, order_book.asks().size());
}

BOOST_AUTO_TEST_CASE(TestLargeSellPartiallyConsumesIceberg)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  SimpleOrder sell(false, 1250, 350);
  {
    SimpleFillCheck fc_sell(&sell, 350, 1250 * 350);
    SimpleFillCheck fc_ice(&iceberg_bid, 350, 1250 * 350);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(1000u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(350u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(650u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 350u), iceberg_bid.filled_cost());
  BOOST_CHECK_EQUAL(simple::os_complete, sell.state());
}

// ---------------------------------------------------------------------------
// Section 5: Price-time priority — regular orders before icebergs
//   At the same price level, a regular order that arrived first should
//   fill before an iceberg that arrived second.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidPriceTimePriority)
{
  SimpleOrderBook order_book;
  SimpleOrder regular_bid(true, 1250, 100);
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &regular_bid, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  SimpleOrder sell(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell, 100, 1250 * 100);
    SimpleFillCheck fc_regular(&regular_bid, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_complete, regular_bid.state());
  BOOST_CHECK_EQUAL(100u, regular_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, regular_bid.open_qty());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(1000u, iceberg_bid.open_qty());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestIcebergAskPriceTimePriority)
{
  SimpleOrderBook order_book;
  SimpleOrder regular_ask(false, 1250, 200);
  SimpleOrder iceberg_ask(false, 1250, 500, 0, book::oc_no_conditions, 50);

  BOOST_CHECK(add_and_verify(order_book, &regular_ask, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  SimpleOrder buy(true, 1250, 200);
  {
    SimpleFillCheck fc_buy(&buy, 200, 1250 * 200);
    SimpleFillCheck fc_regular(&regular_ask, 200, 1250 * 200);
    SimpleFillCheck fc_ice(&iceberg_ask, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &buy, true, true));
  }

  BOOST_CHECK_EQUAL(simple::os_complete, regular_ask.state());
  BOOST_CHECK_EQUAL(200u, regular_ask.filled_qty());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(0u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(500u, iceberg_ask.open_qty());
}

BOOST_AUTO_TEST_CASE(TestIcebergBidTimePriorityOverflowToIceberg)
{
  SimpleOrderBook order_book;
  SimpleOrder regular_bid(true, 1250, 50);
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &regular_bid, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  SimpleOrder sell(false, 1250, 150);
  {
    SimpleFillCheck fc_sell(&sell, 150, 1250 * 150);
    SimpleFillCheck fc_regular(&regular_bid, 50, 1250 * 50);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_complete, regular_bid.state());
  BOOST_CHECK_EQUAL(50u, regular_bid.filled_qty());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(100u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(900u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 100u), iceberg_bid.filled_cost());
  BOOST_CHECK_EQUAL(simple::os_complete, sell.state());
  BOOST_CHECK_EQUAL(150u, sell.filled_qty());
}

// ---------------------------------------------------------------------------
// Section 6: Replenished iceberg and time priority in complex layered books
//   When an iceberg replenishes, its new tip should be treated as a fresh
//   order for time-priority purposes at that price level. This section also
//   tests complex scenarios with layered books, resting icebergs, and large
//   incoming orders that partially consume the book.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergReplenishGoesToBackOfQueue)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 300, 0, book::oc_no_conditions, 100);
  SimpleOrder late_regular(true, 1250, 100);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Fill the first iceberg tip
  SimpleOrder sell1(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell1, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell1, true, true));
  }

  BOOST_CHECK_EQUAL(100u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(200u, iceberg_bid.open_qty());

  // Add a regular order AFTER the iceberg replenishes
  BOOST_CHECK(add_and_verify(order_book, &late_regular, false));

  // Depth: replenished tip 100 + late regular 100 = 200
  DepthCheck<SimpleOrderBook> dc1(order_book.depth());
  BOOST_CHECK(dc1.verify_bid(1250, 2, 200));

  // After re-queue, iceberg keeps its position (re-queued during match).
  // sell2 fills the iceberg's replenished tip first, then late_regular
  // would need another sell.
  SimpleOrder sell2(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell2, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    SimpleFillCheck fc_late(&late_regular, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &sell2, true, true));
  }

  BOOST_CHECK_EQUAL(simple::os_accepted, late_regular.state());
  BOOST_CHECK_EQUAL(0u, late_regular.filled_qty());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(200u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(100u, iceberg_bid.open_qty());

  // Both remain on the book
  DepthCheck<SimpleOrderBook> dc2(order_book.depth());
  BOOST_CHECK(dc2.verify_bid(1250, 2, 200));
}

BOOST_AUTO_TEST_CASE(TestIcebergReplenishAskGoesToBackOfQueue)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 400, 0, book::oc_no_conditions, 100);
  SimpleOrder late_regular_ask(false, 1250, 150);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  // Fill the first tip
  SimpleOrder buy1(true, 1250, 100);
  {
    SimpleFillCheck fc_buy(&buy1, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_ask, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &buy1, true, true));
  }

  BOOST_CHECK_EQUAL(100u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(300u, iceberg_ask.open_qty());

  // Late regular arrives after replenish
  BOOST_CHECK(add_and_verify(order_book, &late_regular_ask, false));

  // Depth: replenished tip 100 + late regular 150 = 250
  DepthCheck<SimpleOrderBook> dc1(order_book.depth());
  BOOST_CHECK(dc1.verify_ask(1250, 2, 250));

  // Buy 100 — iceberg keeps position, fills before late_regular
  SimpleOrder buy2(true, 1250, 100);
  {
    SimpleFillCheck fc_buy(&buy2, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_ask, 100, 1250 * 100);
    SimpleFillCheck fc_late(&late_regular_ask, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &buy2, true, true));
  }

  BOOST_CHECK_EQUAL(simple::os_accepted, late_regular_ask.state());
  BOOST_CHECK_EQUAL(0u, late_regular_ask.filled_qty());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(200u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(200u, iceberg_ask.open_qty());
}

BOOST_AUTO_TEST_CASE(TestIcebergReplenishTwiceWithInterleavedRegulars)
{
  SimpleOrderBook order_book;
  // Iceberg: total=500, tip=100
  SimpleOrder iceberg_bid(true, 1250, 500, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Fill first tip
  SimpleOrder sell1(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell1, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell1, true, true));
  }
  BOOST_CHECK_EQUAL(100u, iceberg_bid.filled_qty());

  // Regular A arrives after first replenish
  SimpleOrder regularA(true, 1250, 75);
  BOOST_CHECK(add_and_verify(order_book, &regularA, false));

  // Sell 75 — iceberg fills first (keeps position), only partial tip consumed
  SimpleOrder sell2(false, 1250, 75);
  {
    SimpleFillCheck fc_sell(&sell2, 75, 1250 * 75);
    SimpleFillCheck fc_ice(&iceberg_bid, 75, 1250 * 75);
    SimpleFillCheck fc_regA(&regularA, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &sell2, true, true));
  }
  BOOST_CHECK_EQUAL(175u, iceberg_bid.filled_qty());

  // Sell 25 to finish the iceberg's current tip — triggers re-queue
  SimpleOrder sell3(false, 1250, 25);
  {
    SimpleFillCheck fc_sell(&sell3, 25, 1250 * 25);
    SimpleFillCheck fc_ice(&iceberg_bid, 25, 1250 * 25);
    BOOST_CHECK(add_and_verify(order_book, &sell3, true, true));
  }
  BOOST_CHECK_EQUAL(200u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(300u, iceberg_bid.open_qty());

  // Now sell 75 — fills regularA (it's ahead after re-queue)
  SimpleOrder sell4(false, 1250, 75);
  {
    SimpleFillCheck fc_sell(&sell4, 75, 1250 * 75);
    SimpleFillCheck fc_regA(&regularA, 75, 1250 * 75);
    SimpleFillCheck fc_ice(&iceberg_bid, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &sell4, true, true));
  }
  BOOST_CHECK_EQUAL(simple::os_complete, regularA.state());
  BOOST_CHECK_EQUAL(200u, iceberg_bid.filled_qty());

  // Verify depth: iceberg tip + nothing else
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestLayeredBookLargeSellPartialFillIcebergRemains)
{
  // Complex layered book: regulars at top, iceberg below at same level
  // A large incoming sell partially fills through regulars and into the iceberg.
  // The iceberg should be the one left with unfilled quantity.
  SimpleOrderBook order_book;

  // Build the bid side: 3 price levels
  SimpleOrder bid_1255_a(true, 1255, 100);
  SimpleOrder bid_1255_b(true, 1255, 150);
  SimpleOrder bid_1252(true, 1252, 200);
  SimpleOrder iceberg_1250(true, 1250, 600, 0, book::oc_no_conditions, 100);
  SimpleOrder regular_1250(true, 1250, 80);

  // Also build some asks for realistic book shape
  SimpleOrder ask_1260(false, 1260, 300);
  SimpleOrder ask_1265(false, 1265, 200);

  BOOST_CHECK(add_and_verify(order_book, &bid_1255_a, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_1255_b, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_1252, false));
  // At 1250: regular arrives first, then iceberg
  BOOST_CHECK(add_and_verify(order_book, &regular_1250, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_1250, false));
  BOOST_CHECK(add_and_verify(order_book, &ask_1260, false));
  BOOST_CHECK(add_and_verify(order_book, &ask_1265, false));

  // Verify initial depth
  DepthCheck<SimpleOrderBook> dc_init(order_book.depth());
  BOOST_CHECK(dc_init.verify_bid(1255, 2, 250));  // 100 + 150
  BOOST_CHECK(dc_init.verify_bid(1252, 1, 200));
  BOOST_CHECK(dc_init.verify_bid(1250, 2, 180));  // 80 regular + 100 iceberg tip
  BOOST_CHECK(dc_init.verify_ask(1260, 1, 300));
  BOOST_CHECK(dc_init.verify_ask(1265, 1, 200));

  // Large sell at 1250 sweeps: 100 + 150 + 200 + 80 + some iceberg = 530+
  // Send 600 — should fill everything above iceberg (530) then 70 from iceberg
  SimpleOrder large_sell(false, 1250, 600);
  {
    SimpleFillCheck fc_sell(&large_sell, 600,
      1255*100 + 1255*150 + 1252*200 + 1250*80 + 1250*70);
    SimpleFillCheck fc_1255a(&bid_1255_a, 100, 1255 * 100);
    SimpleFillCheck fc_1255b(&bid_1255_b, 150, 1255 * 150);
    SimpleFillCheck fc_1252(&bid_1252, 200, 1252 * 200);
    SimpleFillCheck fc_reg1250(&regular_1250, 80, 1250 * 80);
    SimpleFillCheck fc_ice(&iceberg_1250, 70, 1250 * 70);
    BOOST_CHECK(add_and_verify(order_book, &large_sell, true, true));
  }

  // Order reporting: all regulars complete, iceberg partially filled
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1255_a.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1255_b.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1252.state());
  BOOST_CHECK_EQUAL(simple::os_complete, regular_1250.state());
  BOOST_CHECK_EQUAL(simple::os_complete, large_sell.state());
  BOOST_CHECK_EQUAL(600u, large_sell.filled_qty());
  BOOST_CHECK_EQUAL(0u, large_sell.open_qty());

  // The iceberg is the one left with unfilled quantity
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_1250.state());
  BOOST_CHECK_EQUAL(70u, iceberg_1250.filled_qty());
  BOOST_CHECK_EQUAL(530u, iceberg_1250.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 70u), iceberg_1250.filled_cost());

  // Depth: only iceberg remains on bid side (tip = 30 remaining of current tip,
  // or replenished to 100 if the partial fill triggered a replenish —
  // since 70 < tip of 100, no replenish, remaining visible = 30)
  DepthCheck<SimpleOrderBook> dc_after(order_book.depth());
  BOOST_CHECK(dc_after.verify_bid(1250, 1, 30));
  BOOST_CHECK(dc_after.verify_ask(1260, 1, 300));
  BOOST_CHECK(dc_after.verify_ask(1265, 1, 200));
}

BOOST_AUTO_TEST_CASE(TestLayeredBookLargeBuyPartialFillIcebergAskRemains)
{
  // Mirror of above but on the ask side
  SimpleOrderBook order_book;

  // Build the ask side: 3 price levels
  SimpleOrder ask_1250_a(false, 1250, 120);
  SimpleOrder ask_1250_b(false, 1250, 80);
  SimpleOrder ask_1252(false, 1252, 200);
  SimpleOrder regular_1255(false, 1255, 100);
  SimpleOrder iceberg_1255(false, 1255, 800, 0, book::oc_no_conditions, 150);

  // Bid side for realistic book shape
  SimpleOrder bid_1245(true, 1245, 200);

  BOOST_CHECK(add_and_verify(order_book, &ask_1250_a, false));
  BOOST_CHECK(add_and_verify(order_book, &ask_1250_b, false));
  BOOST_CHECK(add_and_verify(order_book, &ask_1252, false));
  // At 1255: regular arrives first, then iceberg
  BOOST_CHECK(add_and_verify(order_book, &regular_1255, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_1255, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_1245, false));

  // Verify initial depth
  DepthCheck<SimpleOrderBook> dc_init(order_book.depth());
  BOOST_CHECK(dc_init.verify_ask(1250, 2, 200));  // 120 + 80
  BOOST_CHECK(dc_init.verify_ask(1252, 1, 200));
  BOOST_CHECK(dc_init.verify_ask(1255, 2, 250));  // 100 regular + 150 iceberg tip
  BOOST_CHECK(dc_init.verify_bid(1245, 1, 200));

  // Large buy at 1255 sweeps: 120 + 80 + 200 + 100 + some iceberg = 500+
  // Send 550 — fills 500 from regulars, then 50 from iceberg
  SimpleOrder large_buy(true, 1255, 550);
  {
    SimpleFillCheck fc_buy(&large_buy, 550,
      1250*120 + 1250*80 + 1252*200 + 1255*100 + 1255*50);
    SimpleFillCheck fc_1250a(&ask_1250_a, 120, 1250 * 120);
    SimpleFillCheck fc_1250b(&ask_1250_b, 80, 1250 * 80);
    SimpleFillCheck fc_1252(&ask_1252, 200, 1252 * 200);
    SimpleFillCheck fc_reg1255(&regular_1255, 100, 1255 * 100);
    SimpleFillCheck fc_ice(&iceberg_1255, 50, 1255 * 50);
    BOOST_CHECK(add_and_verify(order_book, &large_buy, true, true));
  }

  // All regulars complete
  BOOST_CHECK_EQUAL(simple::os_complete, ask_1250_a.state());
  BOOST_CHECK_EQUAL(simple::os_complete, ask_1250_b.state());
  BOOST_CHECK_EQUAL(simple::os_complete, ask_1252.state());
  BOOST_CHECK_EQUAL(simple::os_complete, regular_1255.state());
  BOOST_CHECK_EQUAL(simple::os_complete, large_buy.state());
  BOOST_CHECK_EQUAL(550u, large_buy.filled_qty());

  // The iceberg is the one left
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_1255.state());
  BOOST_CHECK_EQUAL(50u, iceberg_1255.filled_qty());
  BOOST_CHECK_EQUAL(750u, iceberg_1255.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1255u * 50u), iceberg_1255.filled_cost());

  // Depth: iceberg tip partially consumed (100 remaining of tip)
  DepthCheck<SimpleOrderBook> dc_after(order_book.depth());
  BOOST_CHECK(dc_after.verify_ask(1255, 1, 100));
  BOOST_CHECK(dc_after.verify_bid(1245, 1, 200));
}

BOOST_AUTO_TEST_CASE(TestLayeredBookExactFillThroughRegularsIcebergUntouched)
{
  // Large order exactly fills all regular orders but doesn't reach the iceberg
  SimpleOrderBook order_book;

  SimpleOrder bid_1255(true, 1255, 200);
  SimpleOrder bid_1252(true, 1252, 150);
  SimpleOrder iceberg_1250(true, 1250, 1000, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &bid_1255, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_1252, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_1250, false));

  // Sell exactly 350 at 1252 — sweeps 1255 (200) + 1252 (150), stops before iceberg
  SimpleOrder sell(false, 1252, 350);
  {
    SimpleFillCheck fc_sell(&sell, 350, 1255*200 + 1252*150);
    SimpleFillCheck fc_1255(&bid_1255, 200, 1255 * 200);
    SimpleFillCheck fc_1252(&bid_1252, 150, 1252 * 150);
    SimpleFillCheck fc_ice(&iceberg_1250, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1255.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1252.state());
  BOOST_CHECK_EQUAL(simple::os_complete, sell.state());

  // Iceberg completely untouched
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_1250.state());
  BOOST_CHECK_EQUAL(0u, iceberg_1250.filled_qty());
  BOOST_CHECK_EQUAL(1000u, iceberg_1250.open_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_1250.filled_cost());

  // Only iceberg remains
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestLayeredBookSellConsumesFullIcebergTip)
{
  // Large order fills through regulars and exactly consumes one iceberg tip
  SimpleOrderBook order_book;

  SimpleOrder bid_1255(true, 1255, 100);
  SimpleOrder bid_1252(true, 1252, 200);
  SimpleOrder regular_1250(true, 1250, 50);
  SimpleOrder iceberg_1250(true, 1250, 600, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &bid_1255, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_1252, false));
  BOOST_CHECK(add_and_verify(order_book, &regular_1250, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_1250, false));

  // Sell 450 at 1250: fills 100 + 200 + 50 (regulars) + 100 (one full iceberg tip)
  SimpleOrder sell(false, 1250, 450);
  {
    SimpleFillCheck fc_sell(&sell, 450,
      1255*100 + 1252*200 + 1250*50 + 1250*100);
    SimpleFillCheck fc_1255(&bid_1255, 100, 1255 * 100);
    SimpleFillCheck fc_1252(&bid_1252, 200, 1252 * 200);
    SimpleFillCheck fc_reg(&regular_1250, 50, 1250 * 50);
    SimpleFillCheck fc_ice(&iceberg_1250, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // All regulars done
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1255.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid_1252.state());
  BOOST_CHECK_EQUAL(simple::os_complete, regular_1250.state());
  BOOST_CHECK_EQUAL(simple::os_complete, sell.state());
  BOOST_CHECK_EQUAL(450u, sell.filled_qty());

  // Iceberg: one tip consumed, replenished
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_1250.state());
  BOOST_CHECK_EQUAL(100u, iceberg_1250.filled_qty());
  BOOST_CHECK_EQUAL(500u, iceberg_1250.open_qty());

  // Depth shows replenished iceberg tip
  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestLayeredBookMultipleIcebergsBothSides)
{
  // Both sides have icebergs at different levels, regulars interleaved
  SimpleOrderBook order_book;

  // Bid side
  SimpleOrder bid_ice_1255(true, 1255, 400, 0, book::oc_no_conditions, 80);
  SimpleOrder bid_reg_1252(true, 1252, 150);
  SimpleOrder bid_ice_1250(true, 1250, 600, 0, book::oc_no_conditions, 100);

  // Ask side
  SimpleOrder ask_reg_1260(false, 1260, 200);
  SimpleOrder ask_ice_1265(false, 1265, 500, 0, book::oc_no_conditions, 75);

  BOOST_CHECK(add_and_verify(order_book, &bid_ice_1255, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_reg_1252, false));
  BOOST_CHECK(add_and_verify(order_book, &bid_ice_1250, false));
  BOOST_CHECK(add_and_verify(order_book, &ask_reg_1260, false));
  BOOST_CHECK(add_and_verify(order_book, &ask_ice_1265, false));

  // Verify depth shows only tips
  DepthCheck<SimpleOrderBook> dc_init(order_book.depth());
  BOOST_CHECK(dc_init.verify_bid(1255, 1, 80));   // iceberg tip
  BOOST_CHECK(dc_init.verify_bid(1252, 1, 150));  // regular
  BOOST_CHECK(dc_init.verify_bid(1250, 1, 100));  // iceberg tip
  BOOST_CHECK(dc_init.verify_ask(1260, 1, 200));  // regular
  BOOST_CHECK(dc_init.verify_ask(1265, 1, 75));   // iceberg tip

  // Order reporting on all
  BOOST_CHECK_EQUAL(true, bid_ice_1255.is_iceberg());
  BOOST_CHECK_EQUAL(80u, bid_ice_1255.visible_qty());
  BOOST_CHECK_EQUAL(400u, bid_ice_1255.order_qty());
  BOOST_CHECK_EQUAL(false, bid_reg_1252.is_iceberg());
  BOOST_CHECK_EQUAL(true, bid_ice_1250.is_iceberg());
  BOOST_CHECK_EQUAL(true, ask_ice_1265.is_iceberg());
  BOOST_CHECK_EQUAL(75u, ask_ice_1265.visible_qty());
  BOOST_CHECK_EQUAL(false, ask_reg_1260.is_iceberg());

  // Sell at 1252: iceberg at 1255 has 400 total liquidity at the better price,
  // so the entire sell (230) fills from the iceberg, never reaching 1252
  SimpleOrder sell(false, 1252, 230);
  {
    SimpleFillCheck fc_sell(&sell, 230, 1255 * 230);
    SimpleFillCheck fc_ice1255(&bid_ice_1255, 230, 1255 * 230);
    SimpleFillCheck fc_reg1252(&bid_reg_1252, 0, 0);
    SimpleFillCheck fc_ice1250(&bid_ice_1250, 0, 0);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // 1255 iceberg: partially consumed (230 of 400)
  BOOST_CHECK_EQUAL(simple::os_accepted, bid_ice_1255.state());
  BOOST_CHECK_EQUAL(230u, bid_ice_1255.filled_qty());
  BOOST_CHECK_EQUAL(170u, bid_ice_1255.open_qty());

  // 1252 regular: untouched
  BOOST_CHECK_EQUAL(simple::os_accepted, bid_reg_1252.state());
  BOOST_CHECK_EQUAL(0u, bid_reg_1252.filled_qty());

  // 1250 iceberg: untouched
  BOOST_CHECK_EQUAL(0u, bid_ice_1250.filled_qty());
  BOOST_CHECK_EQUAL(600u, bid_ice_1250.open_qty());

  // Depth after partial sweep: iceberg at 1255 shows replenished tip
  DepthCheck<SimpleOrderBook> dc_after(order_book.depth());
  BOOST_CHECK(dc_after.verify_bid(1255, 1, 80));  // replenished tip
  BOOST_CHECK(dc_after.verify_bid(1252, 1, 150)); // untouched regular
  BOOST_CHECK(dc_after.verify_bid(1250, 1, 100)); // untouched iceberg
  BOOST_CHECK(dc_after.verify_ask(1260, 1, 200));
  BOOST_CHECK(dc_after.verify_ask(1265, 1, 75));
}

// ---------------------------------------------------------------------------
// Section 7: Iceberg across multiple price levels
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidMultiplePriceLevels)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_high(true, 1255, 500, 0, book::oc_no_conditions, 50);
  SimpleOrder iceberg_low(true, 1250, 400, 0, book::oc_no_conditions, 100);
  SimpleOrder regular_mid(true, 1252, 200);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_high, false));
  BOOST_CHECK(add_and_verify(order_book, &regular_mid, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_low, false));

  // Order reporting
  BOOST_CHECK_EQUAL(true, iceberg_high.is_iceberg());
  BOOST_CHECK_EQUAL(50u, iceberg_high.visible_qty());
  BOOST_CHECK_EQUAL(500u, iceberg_high.order_qty());
  BOOST_CHECK_EQUAL(false, regular_mid.is_iceberg());
  BOOST_CHECK_EQUAL(200u, regular_mid.order_qty());
  BOOST_CHECK_EQUAL(true, iceberg_low.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg_low.visible_qty());
  BOOST_CHECK_EQUAL(400u, iceberg_low.order_qty());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1255, 1, 50));
  BOOST_CHECK(dc.verify_bid(1252, 1, 200));
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestIcebergAskMultiplePriceLevels)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_low(false, 1250, 600, 0, book::oc_no_conditions, 75);
  SimpleOrder regular_mid(false, 1255, 300);
  SimpleOrder iceberg_high(false, 1260, 400, 0, book::oc_no_conditions, 100);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_low, false));
  BOOST_CHECK(add_and_verify(order_book, &regular_mid, false));
  BOOST_CHECK(add_and_verify(order_book, &iceberg_high, false));

  BOOST_CHECK_EQUAL(true, iceberg_low.is_iceberg());
  BOOST_CHECK_EQUAL(75u, iceberg_low.visible_qty());
  BOOST_CHECK_EQUAL(true, iceberg_high.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg_high.visible_qty());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 1, 75));
  BOOST_CHECK(dc.verify_ask(1255, 1, 300));
  BOOST_CHECK(dc.verify_ask(1260, 1, 100));
}

// ---------------------------------------------------------------------------
// Section 8: Incoming iceberg matches against resting orders
//   An iceberg order that crosses the spread should match like a normal
//   aggressive order, with the full quantity available for matching.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIncomingIcebergBidMatchesRestingAsks)
{
  SimpleOrderBook order_book;
  SimpleOrder ask1(false, 1250, 100);
  SimpleOrder ask2(false, 1251, 200);

  BOOST_CHECK(add_and_verify(order_book, &ask1, false));
  BOOST_CHECK(add_and_verify(order_book, &ask2, false));

  SimpleOrder iceberg_bid(true, 1251, 500, 0, book::oc_no_conditions, 50);
  {
    SimpleFillCheck fc_ice(&iceberg_bid, 300, 1250 * 100 + 1251 * 200);
    SimpleFillCheck fc_ask1(&ask1, 100, 1250 * 100);
    SimpleFillCheck fc_ask2(&ask2, 200, 1251 * 200);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(500u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(300u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(200u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL(simple::os_complete, ask1.state());
  BOOST_CHECK_EQUAL(simple::os_complete, ask2.state());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1251, 1, 50));
}

BOOST_AUTO_TEST_CASE(TestIncomingIcebergAskMatchesRestingBids)
{
  SimpleOrderBook order_book;
  SimpleOrder bid1(true, 1255, 150);
  SimpleOrder bid2(true, 1250, 100);

  BOOST_CHECK(add_and_verify(order_book, &bid1, false));
  BOOST_CHECK(add_and_verify(order_book, &bid2, false));

  SimpleOrder iceberg_ask(false, 1250, 600, 0, book::oc_no_conditions, 100);
  {
    SimpleFillCheck fc_ice(&iceberg_ask, 250, 1255 * 150 + 1250 * 100);
    SimpleFillCheck fc_bid1(&bid1, 150, 1255 * 150);
    SimpleFillCheck fc_bid2(&bid2, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, true));
  }

  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(250u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(350u, iceberg_ask.open_qty());
  BOOST_CHECK_EQUAL(simple::os_complete, bid1.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid2.state());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestIncomingIcebergFullyFilledOnEntry)
{
  SimpleOrderBook order_book;
  SimpleOrder ask(false, 1250, 500);
  BOOST_CHECK(add_and_verify(order_book, &ask, false));

  SimpleOrder iceberg_bid(true, 1250, 300, 0, book::oc_no_conditions, 100);
  {
    SimpleFillCheck fc_ice(&iceberg_bid, 300, 1250 * 300);
    SimpleFillCheck fc_ask(&ask, 300, 1250 * 300);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, true, true));
  }

  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_bid.state());
  BOOST_CHECK_EQUAL(300u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 300u), iceberg_bid.filled_cost());

  // Ask partially filled, still resting
  BOOST_CHECK_EQUAL(simple::os_accepted, ask.state());
  BOOST_CHECK_EQUAL(300u, ask.filled_qty());
  BOOST_CHECK_EQUAL(200u, ask.open_qty());
}

// ---------------------------------------------------------------------------
// Section 9: Iceberg vs iceberg
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidVsIcebergAsk)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 500, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  SimpleOrder iceberg_bid(true, 1250, 300, 0, book::oc_no_conditions, 50);
  {
    SimpleFillCheck fc_bid(&iceberg_bid, 300, 1250 * 300);
    SimpleFillCheck fc_ask(&iceberg_ask, 300, 1250 * 300);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, true, true));
  }

  // Order reporting: bid fully consumed, ask partially
  BOOST_CHECK_EQUAL(simple::os_complete, iceberg_bid.state());
  BOOST_CHECK_EQUAL(300u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 300u), iceberg_bid.filled_cost());

  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(300u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(200u, iceberg_ask.open_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 300u), iceberg_ask.filled_cost());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 1, 100));
}

// ---------------------------------------------------------------------------
// Section 10: Cancel and replace operations on iceberg orders
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestCancelIcebergBid)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 1000, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  BOOST_CHECK(cancel_and_verify(order_book, &iceberg_bid, simple::os_cancelled));
  BOOST_CHECK_EQUAL(0u, order_book.bids().size());

  BOOST_CHECK_EQUAL(simple::os_cancelled, iceberg_bid.state());
  BOOST_CHECK_EQUAL(0u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(true, iceberg_bid.is_iceberg());
}

BOOST_AUTO_TEST_CASE(TestCancelIcebergAsk)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_ask(false, 1250, 500, 0, book::oc_no_conditions, 50);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, false));

  BOOST_CHECK(cancel_and_verify(order_book, &iceberg_ask, simple::os_cancelled));
  BOOST_CHECK_EQUAL(0u, order_book.asks().size());

  BOOST_CHECK_EQUAL(simple::os_cancelled, iceberg_ask.state());
  BOOST_CHECK_EQUAL(0u, iceberg_ask.filled_qty());
}

BOOST_AUTO_TEST_CASE(TestCancelPartiallyFilledIceberg)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_bid(true, 1250, 500, 0, book::oc_no_conditions, 100);
  BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, false));

  // Fill one tip
  SimpleOrder sell(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg_bid, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  // Cancel remaining
  BOOST_CHECK(cancel_and_verify(order_book, &iceberg_bid, simple::os_cancelled));
  BOOST_CHECK_EQUAL(0u, order_book.bids().size());

  // Order reporting: preserves fill history
  BOOST_CHECK_EQUAL(simple::os_cancelled, iceberg_bid.state());
  BOOST_CHECK_EQUAL(100u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 100u), iceberg_bid.filled_cost());
}

// ---------------------------------------------------------------------------
// Section 11: Order reporting / status queries for iceberg orders
//   Dedicated tests for the is_iceberg() and visible_qty() API, plus
//   lifecycle state transitions.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergOrderStatusInitial)
{
  SimpleOrder iceberg(true, 1250, 1000, 0, book::oc_no_conditions, 100);

  BOOST_CHECK_EQUAL(1000u, iceberg.order_qty());
  BOOST_CHECK_EQUAL(1000u, iceberg.open_qty());
  BOOST_CHECK_EQUAL(0u, iceberg.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg.filled_cost());
  BOOST_CHECK_EQUAL(true, iceberg.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg.visible_qty());
  BOOST_CHECK_EQUAL(true, iceberg.is_buy());
  BOOST_CHECK_EQUAL(1250u, iceberg.price());
}

BOOST_AUTO_TEST_CASE(TestIcebergOrderStatusAfterPartialFill)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg(true, 1250, 1000, 0, book::oc_no_conditions, 100);
  SimpleOrder sell(false, 1250, 250);

  BOOST_CHECK(add_and_verify(order_book, &iceberg, false));

  {
    SimpleFillCheck fc_sell(&sell, 250, 1250 * 250);
    SimpleFillCheck fc_ice(&iceberg, 250, 1250 * 250);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }

  BOOST_CHECK_EQUAL(1000u, iceberg.order_qty());
  BOOST_CHECK_EQUAL(750u, iceberg.open_qty());
  BOOST_CHECK_EQUAL(250u, iceberg.filled_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 250u), iceberg.filled_cost());
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg.state());
  BOOST_CHECK_EQUAL(true, iceberg.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg.visible_qty());
}

BOOST_AUTO_TEST_CASE(TestIcebergOrderStatusAfterComplete)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg(false, 1250, 200, 0, book::oc_no_conditions, 100);
  SimpleOrder buy(true, 1250, 200);

  BOOST_CHECK(add_and_verify(order_book, &iceberg, false));

  {
    SimpleFillCheck fc_buy(&buy, 200, 1250 * 200);
    SimpleFillCheck fc_ice(&iceberg, 200, 1250 * 200);
    BOOST_CHECK(add_and_verify(order_book, &buy, true, true));
  }

  BOOST_CHECK_EQUAL(200u, iceberg.order_qty());
  BOOST_CHECK_EQUAL(0u, iceberg.open_qty());
  BOOST_CHECK_EQUAL(200u, iceberg.filled_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 200u), iceberg.filled_cost());
  BOOST_CHECK_EQUAL(simple::os_complete, iceberg.state());
}

BOOST_AUTO_TEST_CASE(TestNonIcebergReportsNotIceberg)
{
  SimpleOrder regular(true, 1250, 100);
  BOOST_CHECK_EQUAL(false, regular.is_iceberg());
  BOOST_CHECK_EQUAL(0u, regular.visible_qty());
  BOOST_CHECK_EQUAL(100u, regular.order_qty());
  BOOST_CHECK_EQUAL(100u, regular.open_qty());
}

// ---------------------------------------------------------------------------
// Section 12: Complex multi-level scenarios
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergBidWithMultipleLevelSweep)
{
  SimpleOrderBook order_book;
  SimpleOrder ask1(false, 1250, 100);
  SimpleOrder ask2(false, 1252, 200);
  SimpleOrder ask3(false, 1255, 150);
  SimpleOrder regular_bid(true, 1245, 100);

  BOOST_CHECK(add_and_verify(order_book, &ask1, false));
  BOOST_CHECK(add_and_verify(order_book, &ask2, false));
  BOOST_CHECK(add_and_verify(order_book, &ask3, false));
  BOOST_CHECK(add_and_verify(order_book, &regular_bid, false));

  SimpleOrder iceberg_bid(true, 1255, 800, 0, book::oc_no_conditions, 100);
  {
    SimpleFillCheck fc_ice(&iceberg_bid, 450, 1250*100 + 1252*200 + 1255*150);
    SimpleFillCheck fc_a1(&ask1, 100, 1250 * 100);
    SimpleFillCheck fc_a2(&ask2, 200, 1252 * 200);
    SimpleFillCheck fc_a3(&ask3, 150, 1255 * 150);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_bid, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_bid.state());
  BOOST_CHECK_EQUAL(800u, iceberg_bid.order_qty());
  BOOST_CHECK_EQUAL(450u, iceberg_bid.filled_qty());
  BOOST_CHECK_EQUAL(350u, iceberg_bid.open_qty());
  BOOST_CHECK_EQUAL(simple::os_complete, ask1.state());
  BOOST_CHECK_EQUAL(simple::os_complete, ask2.state());
  BOOST_CHECK_EQUAL(simple::os_complete, ask3.state());
  BOOST_CHECK_EQUAL(simple::os_accepted, regular_bid.state());
  BOOST_CHECK_EQUAL(0u, regular_bid.filled_qty());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1255, 1, 100));
  BOOST_CHECK(dc.verify_bid(1245, 1, 100));
}

BOOST_AUTO_TEST_CASE(TestIcebergAskWithMultipleLevelSweep)
{
  SimpleOrderBook order_book;
  SimpleOrder bid1(true, 1260, 200);
  SimpleOrder bid2(true, 1255, 150);
  SimpleOrder bid3(true, 1250, 100);
  SimpleOrder regular_ask(false, 1265, 50);

  BOOST_CHECK(add_and_verify(order_book, &bid1, false));
  BOOST_CHECK(add_and_verify(order_book, &bid2, false));
  BOOST_CHECK(add_and_verify(order_book, &bid3, false));
  BOOST_CHECK(add_and_verify(order_book, &regular_ask, false));

  SimpleOrder iceberg_ask(false, 1250, 700, 0, book::oc_no_conditions, 75);
  {
    SimpleFillCheck fc_ice(&iceberg_ask, 450, 1260*200 + 1255*150 + 1250*100);
    SimpleFillCheck fc_b1(&bid1, 200, 1260 * 200);
    SimpleFillCheck fc_b2(&bid2, 150, 1255 * 150);
    SimpleFillCheck fc_b3(&bid3, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_ask, true));
  }

  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_ask.state());
  BOOST_CHECK_EQUAL(700u, iceberg_ask.order_qty());
  BOOST_CHECK_EQUAL(450u, iceberg_ask.filled_qty());
  BOOST_CHECK_EQUAL(250u, iceberg_ask.open_qty());
  BOOST_CHECK_EQUAL(simple::os_complete, bid1.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid2.state());
  BOOST_CHECK_EQUAL(simple::os_complete, bid3.state());
  BOOST_CHECK_EQUAL(simple::os_accepted, regular_ask.state());
  BOOST_CHECK_EQUAL(0u, regular_ask.filled_qty());

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_ask(1250, 1, 75));
  BOOST_CHECK(dc.verify_ask(1265, 1, 50));
}

// ---------------------------------------------------------------------------
// Section 13: Edge cases
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestIcebergVisibleEqualsTotal)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg(true, 1250, 100, 0, book::oc_no_conditions, 100);

  // Degenerate: visible == total means is_iceberg() is false
  BOOST_CHECK_EQUAL(false, iceberg.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg.visible_qty());

  BOOST_CHECK(add_and_verify(order_book, &iceberg, false));

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));

  SimpleOrder sell(false, 1250, 100);
  {
    SimpleFillCheck fc_sell(&sell, 100, 1250 * 100);
    SimpleFillCheck fc_ice(&iceberg, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &sell, true, true));
  }
  BOOST_CHECK_EQUAL(simple::os_complete, iceberg.state());
  BOOST_CHECK_EQUAL(100u, iceberg.filled_qty());
  BOOST_CHECK_EQUAL(0u, iceberg.open_qty());
}

BOOST_AUTO_TEST_CASE(TestIcebergMarketOrder)
{
  SimpleOrderBook order_book;
  SimpleOrder ask(false, 1250, 100);
  BOOST_CHECK(add_and_verify(order_book, &ask, false));

  SimpleOrder iceberg_mkt(true, MARKET_ORDER_PRICE, 300, 0, book::oc_no_conditions, 50);
  {
    SimpleFillCheck fc_ice(&iceberg_mkt, 100, 1250 * 100);
    SimpleFillCheck fc_ask(&ask, 100, 1250 * 100);
    BOOST_CHECK(add_and_verify(order_book, &iceberg_mkt, true));
  }

  // Order reporting
  BOOST_CHECK_EQUAL(100u, iceberg_mkt.filled_qty());
  BOOST_CHECK_EQUAL((book::Cost)(1250u * 100u), iceberg_mkt.filled_cost());
  BOOST_CHECK_EQUAL(simple::os_complete, ask.state());
}

BOOST_AUTO_TEST_CASE(TestIcebergWithStopPrice)
{
  SimpleOrderBook order_book;
  SimpleOrder iceberg_stop(true, 1250, 500, 1240, book::oc_no_conditions, 100);

  // Order reporting: iceberg + stop
  BOOST_CHECK_EQUAL(true, iceberg_stop.is_iceberg());
  BOOST_CHECK_EQUAL(100u, iceberg_stop.visible_qty());
  BOOST_CHECK_EQUAL(1240u, iceberg_stop.stop_price());

  order_book.set_market_price(1245);

  BOOST_CHECK(add_and_verify(order_book, &iceberg_stop, false));

  DepthCheck<SimpleOrderBook> dc(order_book.depth());
  BOOST_CHECK(dc.verify_bid(1250, 1, 100));

  BOOST_CHECK_EQUAL(simple::os_accepted, iceberg_stop.state());
  BOOST_CHECK_EQUAL(500u, iceberg_stop.open_qty());
}

} // namespace liquibook
