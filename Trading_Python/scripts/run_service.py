#!/usr/bin/env python
"""
Trading-Python Service Runner

Starts the FastAPI server with background scheduler.

Usage:
    python scripts/run_service.py
    python scripts/run_service.py --port 8000
    python scripts/run_service.py --host 0.0.0.0 --port 8080
"""

import argparse
import os
import sys
import signal
import logging

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from src.service.scheduler import start_scheduler, stop_scheduler, get_scheduler

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


def parse_args():
    parser = argparse.ArgumentParser(description="Start Trading-Python API Service")

    parser.add_argument(
        "--host",
        type=str,
        default="0.0.0.0",
        help="Host to bind to (default: 0.0.0.0)"
    )

    parser.add_argument(
        "--port",
        type=int,
        default=8000,
        help="Port to bind to (default: 8000)"
    )

    parser.add_argument(
        "--reload",
        action="store_true",
        help="Enable auto-reload (development mode)"
    )

    parser.add_argument(
        "--no-scheduler",
        action="store_true",
        help="Disable background scheduler"
    )

    parser.add_argument(
        "--log-level",
        type=str,
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging level"
    )

    return parser.parse_args()


def main():
    args = parse_args()

    # Set log level
    logging.getLogger().setLevel(getattr(logging, args.log_level))

    logger.info("=" * 60)
    logger.info("Trading-Python API Service")
    logger.info("=" * 60)
    logger.info(f"Host: {args.host}")
    logger.info(f"Port: {args.port}")
    logger.info(f"Scheduler: {'enabled' if not args.no_scheduler else 'disabled'}")
    logger.info("=" * 60)

    # Start background scheduler
    scheduler = None
    if not args.no_scheduler:
        try:
            scheduler = start_scheduler()
            logger.info("Background scheduler started")

            # Print scheduled jobs
            jobs = scheduler.get_jobs()
            logger.info(f"Scheduled {len(jobs)} jobs:")
            for job in jobs:
                logger.info(f"  - {job['name']}: next run at {job['next_run']}")

        except Exception as e:
            logger.error(f"Failed to start scheduler: {e}")
            logger.warning("Continuing without scheduler...")

    # Setup signal handlers for graceful shutdown
    def signal_handler(signum, frame):
        logger.info("Received shutdown signal...")
        if scheduler:
            stop_scheduler()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Import and run uvicorn
    try:
        import uvicorn
    except ImportError:
        logger.error("uvicorn not installed. Run: pip install uvicorn")
        sys.exit(1)

    # Get the app
    from src.api.main import app

    # Run the server
    logger.info(f"Starting server on http://{args.host}:{args.port}")
    logger.info(f"API docs available at http://{args.host}:{args.port}/docs")

    uvicorn.run(
        app,
        host=args.host,
        port=args.port,
        reload=args.reload,
        log_level=args.log_level.lower()
    )


if __name__ == "__main__":
    main()
