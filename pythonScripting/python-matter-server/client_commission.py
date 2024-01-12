import argparse
import asyncio
import logging
import os

import aiohttp
from aiorun import run
import coloredlogs

from matter_server.client.client import MatterClient
from matter_server.common.errors import NodeCommissionFailed

logging.basicConfig(level=logging.DEBUG)
_LOGGER = logging.getLogger(__name__)

DEFAULT_PORT = 5580
DEFAULT_URL = f"http://127.0.0.1:{DEFAULT_PORT}/ws"


# Get parsed passed in arguments.
parser = argparse.ArgumentParser(description="Matter Client - Commissioning.")
parser.add_argument(
    "--port",
    type=int,
    default=DEFAULT_PORT,
    help=f"TCP Port on which to run the Matter WebSockets Server, defaults to {DEFAULT_PORT}",
)
parser.add_argument(
    "--log-level",
    type=str,
    default="info",
    help="Provide logging level. Example --log-level debug, default=info, possible=(critical, error, warning, info, debug)",
)
parser.add_argument(
    "--code",
    type=str,
    default="MT:Y.K9042C00KA0648G00",
    help="Provide the code for commissioning.",
)

args = parser.parse_args()


if __name__ == "__main__":
    # configure logging
    logging.basicConfig(level=args.log_level.upper())
    coloredlogs.install(level=args.log_level.upper())

    async def commission(client: MatterClient, code: str):
        # set credentials
        await client.set_wifi_credentials(ssid="__REPLACE_ME__", credentials="__REPLACE_ME__")
        await client.set_thread_operational_dataset(dataset="__REPLACE_ME__")
        # commission
        await client.commission_with_code(code)

    async def run_matter():
        """Run the Matter client."""

        # run the client
        url = f"http://127.0.0.1:{args.port}/ws"
        async with aiohttp.ClientSession() as session:
            async with MatterClient(url, session) as client:

                # start listening and continue when the client is initialized
                ready = asyncio.Event()
                asyncio.create_task(client.start_listening(init_ready=ready))
                await ready.wait()

                logging.info(f"Server Info: {client.server_info}")

                try:
                    await commission(client, args.code)
                except NodeCommissionFailed:
                    logging.error("Commissioning failed")

                # The only way I found to exit ...
                os._exit(0)

    # run the client interaction
    run(run_matter())
