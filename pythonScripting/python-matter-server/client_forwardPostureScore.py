import argparse
import asyncio
import logging
import os
from pathlib import Path

import aiohttp
from aiorun import run
import coloredlogs

from matter_server.client.client import MatterClient
from matter_server.common.helpers.util import create_attribute_path_from_attribute
from matter_server.common.models import EventType
from chip.clusters import Objects as Clusters

from datetime import datetime, timedelta, timezone

logging.basicConfig(level=logging.DEBUG)
_LOGGER = logging.getLogger(__name__)

DEFAULT_PORT = 5580
DEFAULT_URL = f"http://127.0.0.1:{DEFAULT_PORT}/ws"


# Get parsed passed in arguments.
parser = argparse.ArgumentParser(description="Matter Client - Forward Posture Score")
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

args = parser.parse_args()


if __name__ == "__main__":
    # configure logging
    logging.basicConfig(level=args.log_level.upper())
    coloredlogs.install(level=args.log_level.upper())

    queue = asyncio.Queue()

    acceptNextValueAfter = datetime.now(timezone.utc)

    # Helper function since the client does not provide
    # the information on which endpoint a cluster is found.
    def findFirstEndpointForCluster(node, cluster):
        for endpoint in node.endpoints.values():
            if(endpoint.has_cluster(cluster)):
                return endpoint.endpoint_id
        return None        

    def handleEvent(eventType, value):
        global acceptNextValueAfter
        # Unfortunately there is no info which attribute has been updated here
        # On device reset, all values arrive here.
        # Workaround to skip all events for 3 second as soon as an array type or string arrives.
        if eventType == EventType.ATTRIBUTE_UPDATED:
            # Arrays, Dictionaries and Strings have the attribute __len__
            if hasattr(value, "__len__") or value is None:
                acceptNextValueAfter = datetime.now(timezone.utc) + timedelta(seconds=3)
            if datetime.now(timezone.utc) > acceptNextValueAfter:
                logging.info(f"eventType={eventType}, value={value}")
                queue.put_nowait(value)

    async def run_matter():
        # run the Matter client, conect to the Matter server.
        url = f"http://127.0.0.1:{args.port}/ws"
        async with aiohttp.ClientSession() as session:
            async with MatterClient(url, session) as client:

                # start listening and continue when the client is initialized
                ready = asyncio.Event()
                asyncio.create_task(client.start_listening(init_ready=ready))
                await ready.wait()

                logging.info(f"Server Info: {client.server_info}")

                # Subscribe to Matter server events
                client.subscribe_events(callback=handleEvent)

                bridge_node_id = None
                bridge_currentLevel_path = None
                feedbackLight_node_id = None
                feedbackLight_colorControl_endpoint = None

                for node in client.get_nodes():
                    logging.info(f"Node ID={node.node_id}, name={node.name}, available={node.available}, device_info={node.device_info}, is_bridge_device={node.is_bridge_device}, endpoints={node.endpoints.values()}")
                    logging.info(f"has cluster OnOff {node.has_cluster(cluster=Clusters.OnOff)}")
                    logging.info(f"has cluster ColorControl {node.has_cluster(cluster=Clusters.ColorControl)}")
                    logging.info(f"is bridge {node.is_bridge_device}")

                    # Since the Matter devices do not provide names (to be implemented) detect devices based on clusters and attributes.
                    if(node.is_bridge_device):
                        # This is the Bridge, subscribe to the current level of the level control cluster
                        bridge_node_id = node.node_id
                        bridge_currentLevel_path = create_attribute_path_from_attribute(
                            3,
                            Clusters.LevelControl.Attributes.CurrentLevel
                        )
                        # Test read.
                        value = await client.read_attribute(
                            bridge_node_id,
                            bridge_currentLevel_path
                        )                        
                        logging.info(f"Vaue was {value}. Subscribe to path {bridge_currentLevel_path}")
                        await client.subscribe_attribute(
                            bridge_node_id,
                            bridge_currentLevel_path
                        )
                    if(node.has_cluster(cluster=Clusters.ColorControl)):
                        # This is the Feedback Light
                        feedbackLight_node_id = node.node_id
                        feedbackLight_colorControl_endpoint = findFirstEndpointForCluster(node=node, cluster=Clusters.ColorControl)
                        
                        # Set a level, so that the LEDs are visible
                        logging.info(f"move to level 20")
                        await client.send_device_command(
                            feedbackLight_node_id,
                            findFirstEndpointForCluster(node=node, cluster=Clusters.LevelControl),
                            Clusters.LevelControl.Commands.MoveToLevel(
                                level=20,
                                transitionTime=0,
                            )
                        )

                        # Turn the LEDs on. Later only the Hue is changed.
                        logging.info(f"switch on")
                        await client.send_device_command(
                            feedbackLight_node_id,
                            findFirstEndpointForCluster(node=node, cluster=Clusters.OnOff),
                            Clusters.OnOff.Commands.On(),
                        )                        
                
                # Handle the attribute changes
                while True:

                    def mapPostureScoreLevelToHue(level):
                        # Posture score range from 0 (good) to 255 (bad); 256 steps; stepping in positive direction
                        # Hue range from 166 (good) to 216 (bad); 205 steps; stepping in negative direction; roll over at 0 to 255
                        rangeMultiplicator = 205 / 256
                        hueGoodPoint = 166
                        hue = hueGoodPoint - (level * rangeMultiplicator)
                        if hue < 0:
                            hue = 255 + hue
                        return hue

                    levelValue = await queue.get()
                    hueValue = mapPostureScoreLevelToHue(levelValue)
                    logging.info(f"mapped level {levelValue} to hue {int(hueValue)}")
                    try:
                        await client.send_device_command(
                            feedbackLight_node_id,
                            feedbackLight_colorControl_endpoint,
                            Clusters.ColorControl.Commands.MoveToHue(
                                hue=int(hueValue),
                                direction=1,
                                transitionTime=1,
                                optionsMask=0,
                                optionsOverride=0,
                            )
                        )
                    except:
                        logging.error(f"Sending move to hue command failed.")

    async def handle_stop(loop: asyncio.AbstractEventLoop):
        """Handle server stop."""
        logging.info("exit")
        # sys.exit() does not work
        os._exit(0)

    # run the server
    run(run_matter(), shutdown_callback=handle_stop)