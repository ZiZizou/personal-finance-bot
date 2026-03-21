"""
Market hours utility for Trading-Python.

Provides functions to check if the US stock market is currently open.
Based on NYSE regular hours: Mon-Fri, 9:30 AM - 4:00 PM Eastern Time
Extended hours: Mon-Fri, 8:00 AM - 6:00 PM Eastern Time
"""

import os
import sys
from datetime import datetime, time
from typing import Optional

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def get_eastern_time() -> datetime:
    """
    Get current time in Eastern Time (handles EST/EDT automatically).

    Returns:
        datetime object in Eastern Time
    """
    try:
        import pytz
        eastern = pytz.timezone('US/Eastern')
        utc = pytz.utc
        now_utc = datetime.now(utc)
        return now_utc.astimezone(eastern)
    except ImportError:
        # Fallback: assume UTC-5 (EST) without DST handling
        import time
        now = datetime.now()
        # Get offset from system (approximate)
        epoch = time.time()
        offset_seconds = time.timezone if (time.localtime(epoch).tm_isdst == 0) else time.altzone
        offset_hours = -offset_seconds / 3600  # Convert to positive for EST
        # Apply rough EST offset (5 hours behind UTC)
        from datetime import timedelta
        est_offset = timedelta(hours=5)
        return now - est_offset


def is_market_open(now: Optional[datetime] = None) -> bool:
    """
    Check if the market is currently open (regular session).

    Regular session: Mon-Fri, 9:30 AM - 4:00 PM Eastern Time

    Args:
        now: Optional datetime in Eastern Time. If None, uses current time.

    Returns:
        True if market is open, False otherwise
    """
    if now is None:
        now = get_eastern_time()

    # Check weekday (0=Monday, 4=Friday, 5=Saturday, 6=Sunday)
    dow = now.weekday()
    if dow >= 5:  # Saturday or Sunday
        return False

    # Check time
    market_open = time(9, 30)  # 9:30 AM
    market_close = time(16, 0)  # 4:00 PM

    current_time = now.time()
    return market_open <= current_time < market_close


def is_extended_hours(now: Optional[datetime] = None) -> bool:
    """
    Check if we're in extended trading hours.

    Extended hours: Mon-Fri, 8:00 AM - 6:00 PM Eastern Time

    Args:
        now: Optional datetime in Eastern Time. If None, uses current time.

    Returns:
        True if in extended hours, False otherwise
    """
    if now is None:
        now = get_eastern_time()

    # Check weekday
    dow = now.weekday()
    if dow >= 5:  # Saturday or Sunday
        return False

    # Check time
    extended_open = time(8, 0)   # 8:00 AM
    extended_close = time(18, 0)  # 6:00 PM

    current_time = now.time()
    return extended_open <= current_time < extended_close


def is_trading_day(now: Optional[datetime] = None) -> bool:
    """
    Check if today is a trading day (weekday).

    Args:
        now: Optional datetime. If None, uses current time.

    Returns:
        True if today is a trading day, False otherwise
    """
    if now is None:
        now = get_eastern_time()

    # Monday=0, Sunday=6
    return now.weekday() < 5


def get_next_open() -> datetime:
    """
    Get the next market open time.

    Returns:
        datetime of next market open in Eastern Time
    """
    now = get_eastern_time()

    # If it's a weekday and before 9:30 AM, return today at 9:30 AM
    if is_trading_day(now) and now.time() < time(9, 30):
        return now.replace(hour=9, minute=30, second=0, microsecond=0)

    # Otherwise, find next trading day
    from datetime import timedelta
    next_day = now + timedelta(days=1)

    # Fast-forward to next weekday
    while next_day.weekday() >= 5:
        next_day += timedelta(days=1)

    return next_day.replace(hour=9, minute=30, second=0, microsecond=0)


def get_next_close() -> datetime:
    """
    Get the next market close time.

    Returns:
        datetime of next market close in Eastern Time
    """
    now = get_eastern_time()

    # If it's a weekday and before 4:00 PM, return today at 4:00 PM
    if is_trading_day(now) and now.time() < time(16, 0):
        return now.replace(hour=16, minute=0, second=0, microsecond=0)

    # Otherwise, find next trading day
    from datetime import timedelta
    next_day = now + timedelta(days=1)

    # Fast-forward to next weekday
    while next_day.weekday() >= 5:
        next_day += timedelta(days=1)

    return next_day.replace(hour=16, minute=0, second=0, microsecond=0)


def wait_for_market_open(poll_interval: int = 60) -> None:
    """
    Wait until market opens.

    Args:
        poll_interval: How often to check (in seconds)
    """
    import time as time_module

    while not is_market_open():
        next_open = get_next_open()
        now = get_eastern_time()
        wait_seconds = (next_open - now).total_seconds()
        print(f"Market closed. Waiting {wait_seconds/60:.1f} minutes until {next_open.strftime('%H:%M %Z')}")
        time_module.sleep(min(wait_seconds, poll_interval))


def should_run_scheduled_task() -> bool:
    """
    Check if scheduled tasks should run.

    Returns True during extended hours on trading days.

    Returns:
        True if scheduled tasks should run, False otherwise
    """
    return is_extended_hours()


if __name__ == "__main__":
    # Test the market hours functions
    now = get_eastern_time()
    print(f"Current time (ET): {now.strftime('%Y-%m-%d %H:%M:%S %Z')}")
    print(f"Day of week: {now.strftime('%A')}")
    print(f"Market open: {is_market_open()}")
    print(f"Extended hours: {is_extended_hours()}")
    print(f"Trading day: {is_trading_day()}")
    print(f"Should run scheduled: {should_run_scheduled_task()}")
    print(f"Next open: {get_next_open().strftime('%Y-%m-%d %H:%M %Z')}")
    print(f"Next close: {get_next_close().strftime('%Y-%m-%d %H:%M %Z')}")
