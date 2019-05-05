/*
 * This source file is part of an OSTIS project. For the latest info, see http://ostis.net
 * Distributed under the MIT License
 * (See accompanying file COPYING.MIT or copy at http://opensource.org/licenses/MIT)
 */

#pragma once

#include "sc_event.hpp"

extern "C"
{
#include <glib.h>
}

/* Class implements common wait logic.
 */
class ScWait
{
  class WaiterImpl
  {
  public:
    WaiterImpl()
      : m_isResolved(false)
    {
      g_mutex_init(&m_mutex);
      g_cond_init(&m_cond);
    }

    ~WaiterImpl()
    {
      g_mutex_clear(&m_mutex);
      g_cond_clear(&m_cond);
    }

    void Resolve()
    {
      g_mutex_lock(&m_mutex);
      m_isResolved = true;
      g_cond_signal(&m_cond);
      g_mutex_unlock(&m_mutex);
    }

    bool Wait(uint32_t timeout_ms)
    {
      gint64 endTime;

      g_mutex_lock(&m_mutex);
      endTime = g_get_monotonic_time() + timeout_ms * G_TIME_SPAN_MILLISECOND;
      while (!m_isResolved)
      {
        if (!g_cond_wait_until(&m_cond, &m_mutex, endTime))
        {
          g_mutex_unlock(&m_mutex);
          return false;
        }
      }
      g_mutex_unlock(&m_mutex);

      return true;
    }

  private:
    GMutex m_mutex;
    GCond m_cond;
    bool m_isResolved : 1;
  };

public:
  using DelegateFunc = std::function<void(void)>;

  virtual ~ScWait() {}

  void Resolve()
  {
    m_waiterImpl.Resolve();
  }

  void SetOnWaitStartDelegate(DelegateFunc const & startDelegate)
  {
    m_waitStartDelegate = startDelegate;
  }

  bool Wait(uint32_t timeout_ms = 5000)
  {
    if (m_waitStartDelegate)
      m_waitStartDelegate();

    SC_ASSERT(timeout_ms < 60000, ("Too big timeout (it should be less then a minute)"));

    return m_waiterImpl.Wait(timeout_ms);
  }

private:
  WaiterImpl m_waiterImpl;
  DelegateFunc m_waitStartDelegate;
};

/* Class implements event wait logic.
* Should be alive, while Memory context is alive.
*/
template <typename EventClassT>
class ScWaitEvent : public ScWait
{
public:
  ScWaitEvent(const ScMemoryContext & ctx, const ScAddr & addr)
    : m_event(ctx, addr,
              std::bind(&ScWaitEvent<EventClassT>::OnEvent,
                        this,
                        std::placeholders::_1,
                        std::placeholders::_2,
                        std::placeholders::_3))
  {
  }

  virtual ~ScWaitEvent() {}

protected:
  bool OnEvent(ScAddr const & listenAddr, ScAddr const & edgeAddr, ScAddr const & otherAddr)
  {
    if (OnEventImpl(listenAddr, edgeAddr, otherAddr))
    {
      ScWait::Resolve();
      return true;
    }
    return false;
  }

  virtual bool OnEventImpl(ScAddr const & listenAddr, ScAddr const & edgeAddr, ScAddr const & otherAddr) { return true; }

private:
  EventClassT m_event;
};


template<typename EventClassT>
class ScWaitCondition final : public ScWaitEvent <EventClassT>
{
public:

  using DelegateCheckFunc = std::function<bool(ScAddr const &, ScAddr const &, ScAddr const &)>;

  ScWaitCondition(const ScMemoryContext & ctx, const ScAddr & addr, DelegateCheckFunc func)
    : ScWaitEvent<EventClassT>(ctx, addr)
    , m_checkFunc(func)
  {
  }

private:
  virtual bool OnEventImpl(ScAddr const & listenAddr, ScAddr const & edgeAddr, ScAddr const & otherAddr) override
  {
    return m_checkFunc(listenAddr, edgeAddr, otherAddr);
  }

private:
  DelegateCheckFunc m_checkFunc;
};

/* Implements waiting for action finish
 */
class ScWaitActionFinished final : public ScWaitEvent<ScEventAddInputEdge>
{
public:
  _SC_EXTERN ScWaitActionFinished(ScMemoryContext const & ctx, ScAddr const & actionAddr);

private:
  virtual bool OnEventImpl(ScAddr const & listenAddr, ScAddr const & edgeAddr, ScAddr const & otherAddr) override;
};

#define SC_WAIT_CHECK(_func) std::bind(_func, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
#define SC_WAIT_CHECK_MEMBER(_class, _func) std::bind(_class, _func, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
