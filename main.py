#!/usr/bin/env python3
# -*- coding:utf-8 -*-
"""
Main Python Entry Point for Waveshare PoE HAT (B)
Supports configurable thresholds, graceful shutdown, and predictive cooling.
"""

import sys
import os
import time
import signal
import logging
import argparse

# Add package directory to python path if needed
pkg_dir = os.path.dirname(os.path.realpath(__file__))
if pkg_dir not in sys.path:
    sys.path.insert(0, pkg_dir)

from waveshare_POE_HAT_B import POE_HAT_B

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("poe_hat")


def main():
    parser = argparse.ArgumentParser(description="Waveshare PoE HAT (B) Python Controller")
    parser.add_argument("-t", "--temp", type=float, default=54.0, help="Fan activation temperature threshold in Celsius (default: 54)")
    parser.add_argument("-i", "--interval", type=float, default=2.0, help="Refresh loop interval in seconds (default: 2.0)")
    parser.add_argument("-c", "--cooldown", type=int, default=25, help="Minimum fan running time in seconds (default: 25)")
    args = parser.parse_args()

    logger.info("Initializing Waveshare PoE HAT (B) controller...")
    poe = POE_HAT_B.POE_HAT_B()
    poe.cooldown_sec = args.cooldown

    running = True

    def sig_handler(signum, frame):
        nonlocal running
        logger.info("Received termination signal (%d), shutting down cleanly...", signum)
        running = False

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)

    logger.info("Controller started. Target Temp: %.1f C | Interval: %.1f s", args.temp, args.interval)

    try:
        while running:
            poe.POE_HAT_Display(args.temp)
            time.sleep(args.interval)
    except Exception as e:
        logger.error("Unexpected error in main loop: %s", e)
    finally:
        logger.info("Cleaning up hardware and turning off fan...")
        poe.cleanup()
        logger.info("Controller stopped.")


if __name__ == '__main__':
    main()
