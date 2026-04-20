// Copyright (c) 2012, 2013 Object Computing, Inc.
// All rights reserved.
// See the file license.txt for licensing information.
#pragma once

#include "order_book.h"
#include "depth.h"
#include <algorithm>
#include "bbo_listener.h"
#include "depth_listener.h"

namespace liquibook { namespace book {

/// @brief Implementation of order book child class, that incorporates
///        aggregate depth tracking.  
template <typename OrderPtr, int SIZE = 5>
class DepthOrderBook : public OrderBook<OrderPtr> {
public:
  typedef Depth<SIZE> DepthTracker;
  typedef BboListener<DepthOrderBook >TypedBboListener;
  typedef DepthListener<DepthOrderBook >TypedDepthListener;

  /// @brief construct
  DepthOrderBook(const std::string & symbol = "unknown");

  /// @brief set the BBO listener
  void set_bbo_listener(TypedBboListener* bbo_listener);

  /// @brief set the depth listener
  void set_depth_listener(TypedDepthListener* depth_listener);

  // @brief access the depth tracker
  DepthTracker& depth();

  // @brief access the depth tracker
  const DepthTracker& depth() const;

  protected:
  //////////////////////////////////
  // Implement virtual callback methods
  // needed to maintain depth book.
  virtual void on_accept(const OrderPtr& order, Quantity quantity);
  virtual void on_accept_stop(const OrderPtr& order);
  virtual void on_trigger_stop(const OrderPtr& order);

  virtual void on_fill(const OrderPtr& order, 
    const OrderPtr& matched_order, 
    Quantity fill_qty, 
    Price fill_price,
    bool inbound_order_filled,
    bool matched_order_filled);

  virtual void on_cancel(const OrderPtr& order, Quantity quantity);
  virtual void on_cancel_stop(const OrderPtr& order);

  virtual void on_replace(const OrderPtr& order,
    Quantity current_qty, 
    Quantity new_qty,
    Price new_price);

  virtual void on_order_book_change();

private:
  DepthTracker depth_;
  TypedBboListener* bbo_listener_;
  TypedDepthListener* depth_listener_;
};

template <class OrderPtr, int SIZE>
DepthOrderBook<OrderPtr, SIZE>::DepthOrderBook(const std::string & symbol)
: OrderBook<OrderPtr>(symbol),
  bbo_listener_(nullptr),
  depth_listener_(nullptr)
{
}

template <class OrderPtr, int SIZE>
void
DepthOrderBook<OrderPtr, SIZE>::set_bbo_listener(TypedBboListener* listener)
{
  bbo_listener_ = listener;
}

template <class OrderPtr, int SIZE>
void
DepthOrderBook<OrderPtr, SIZE>::set_depth_listener(TypedDepthListener* listener)
{
  depth_listener_ = listener;
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_accept(const OrderPtr& order, Quantity quantity)
{
  // If the order is a limit order
  if (order->is_limit())
  {
    // If the order is completely filled on acceptance, do not modify 
    // depth unnecessarily
    Quantity display_qty = order->order_qty();
    if(order->visible_qty() > 0 && order->visible_qty() < order->order_qty()) {
      display_qty = order->visible_qty();
    }
    if (quantity == order->order_qty())
    {
      depth_.ignore_fill_qty(display_qty, order->is_buy());
    }
    else
    {
      depth_.add_order(order->price(),
        display_qty,
        order->is_buy());
    }
  }
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_accept_stop(const OrderPtr& order)
{
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_trigger_stop(const OrderPtr& order)
{
  Quantity display_qty = order->order_qty();
  if(order->visible_qty() > 0 && order->visible_qty() < order->order_qty()) {
    display_qty = order->visible_qty();
  }
  depth_.add_order(order->price(), display_qty, order->is_buy());
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_fill(const OrderPtr& order, 
  const OrderPtr& matched_order, 
  Quantity quantity, 
  Price fill_price,
  bool inbound_order_filled,
  bool matched_order_filled)
{
  // If the matched order is a limit order
  if (matched_order->is_limit()) {
    bool matched_is_iceberg = matched_order->visible_qty() > 0
                           && matched_order->visible_qty() < matched_order->order_qty();
    if(matched_is_iceberg) {
      Quantity vis = matched_order->visible_qty();
      Quantity filled_before = matched_order->filled_qty();
      Quantity filled_after = filled_before + quantity;
      // How much of the current depth tip is consumed by this fill
      Quantity tip_before = filled_before % vis;
      Quantity tip_fill = (std::min)(quantity, vis - tip_before);
      // Did this fill cross a tip boundary?
      bool tip_crossed = (filled_after / vis) > (filled_before / vis)
                      || (filled_after == matched_order->order_qty());
      depth_.fill_order(matched_order->price(),
        tip_fill,
        matched_order_filled,
        matched_order->is_buy());
      if(!matched_order_filled && tip_crossed) {
        Quantity remaining = matched_order->order_qty() - filled_after;
        Quantity next_tip = (std::min)(vis, remaining);
        depth_.change_qty_order(matched_order->price(), next_tip, matched_order->is_buy());
      }
    } else {
      depth_.fill_order(matched_order->price(),
        quantity,
        matched_order_filled,
        matched_order->is_buy());
    }
  }
  // If the inbound order is a limit order
  if (order->is_limit()) {
    bool inbound_is_iceberg = order->visible_qty() > 0
                           && order->visible_qty() < order->order_qty();
    if(inbound_is_iceberg) {
      Quantity depth_fill = (std::min)(quantity, order->visible_qty());
      depth_.fill_order(order->price(),
        depth_fill,
        inbound_order_filled,
        order->is_buy());
      if(!inbound_order_filled && order->open_qty() > 0) {
        Quantity total_filled = order->filled_qty() + quantity;
        Quantity remaining = order->order_qty() - total_filled;
        Quantity next_tip = (std::min)(order->visible_qty(), remaining);
        depth_.change_qty_order(order->price(), next_tip, order->is_buy());
      }
    } else {
      depth_.fill_order(order->price(),
        quantity,
        inbound_order_filled,
        order->is_buy());
    }
  }
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_cancel(const OrderPtr& order, Quantity quantity)
{
  // If the order is a limit order
  if (order->is_limit()) {
    Quantity depth_qty = quantity;
    if(order->visible_qty() > 0 && order->visible_qty() < order->order_qty()) {
      // For icebergs, only the current visible tip is tracked in depth
      // quantity here is the open_qty from the tracker which includes hidden
      // We need to figure out what's actually showing in depth
      Quantity filled = order->order_qty() - quantity;
      Quantity remaining_total = quantity;
      // Current tip showing = min(visible_qty, remaining_total)
      // But we need to account for partial tip consumption
      Quantity full_tips_consumed = filled / order->visible_qty();
      Quantity partial_consumed = filled % order->visible_qty();
      Quantity current_tip;
      if(partial_consumed > 0) {
        current_tip = order->visible_qty() - partial_consumed;
      } else {
        current_tip = (std::min)(order->visible_qty(), remaining_total);
      }
      depth_qty = current_tip;
    }
    depth_.close_order(order->price(),
      depth_qty,
      order->is_buy());
  }
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_cancel_stop(const OrderPtr& order)
{
  // nothing to do for STOP until triggered/submitted
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_replace(const OrderPtr& order,
  Quantity current_qty, 
  Quantity new_qty,
  Price new_price)
{
  // Notify the depth
  depth_.replace_order(order->price(), new_price, 
    current_qty, new_qty, order->is_buy());
}

template <class OrderPtr, int SIZE> 
void 
DepthOrderBook<OrderPtr, SIZE>::on_order_book_change()
{
  // Book was updated, see if the depth we track was effected
  if (depth_.changed()) {
    if (depth_listener_) {
      depth_listener_->on_depth_change(this, &depth_);
    }
    if (bbo_listener_) {
      ChangeId last_change = depth_.last_published_change();
      // May have been the first level which changed
      if ((depth_.bids()->changed_since(last_change)) ||
        (depth_.asks()->changed_since(last_change))) {
        bbo_listener_->on_bbo_change(this, &depth_);
      }
    }
    // Start tracking changes again...
    depth_.published();
  }
}

template <class OrderPtr, int SIZE>
inline typename DepthOrderBook<OrderPtr, SIZE>::DepthTracker&
DepthOrderBook<OrderPtr, SIZE>::depth()
{
  return depth_;
}

template <class OrderPtr, int SIZE>
inline const typename DepthOrderBook<OrderPtr, SIZE>::DepthTracker&
DepthOrderBook<OrderPtr, SIZE>::depth() const
{
  return depth_;
}

} }
