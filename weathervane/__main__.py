#! python3

import argparse
import configparser
import datetime
import json
import logging
import os
from typing import Any

from http.server import BaseHTTPRequestHandler, HTTPServer
import requests


log = None
APP = "weathervane"
port = 12345


class WeathervaneError(BaseException):
    """General internal error class. May do something fancy later"""

    reason: str | None = None

    def __init__(self, reason: str):
        self.reason = reason

    def __str__(self):
        return self.reason


def get_logging(level=None) -> logging.Logger:
    """Initialize the logger.
    This uses my standard `PYTHON_LOG` env var to set the level.
    """
    base = "DEBUG"
    if not level:
        level = os.environ.get(
            "PYTHON_LOG", "DEBUG"
        ).upper()
    logging.basicConfig(level=logging.getLevelNamesMapping().get(level, base))
    return logging.getLogger(APP)


def get_settings():
    """Gather all ye settings where-ever they may be.

    TODO: I should probably convert to EnvArgParse or something to read the configs
    out of the environment and CLI, but I really do prefer having these in a file.
    """
    aparse = argparse.ArgumentParser(prog=APP)
    aparse.add_argument(
        "-c" "--config", help="configuration file", default="settings.toml"
    )
    args = aparse.parse_args()
    config = configparser.ConfigParser()
    config.read(args.c__config)
    return config


def req(url):
    """Dumb wrapper for a get request to add the API `User-Agent` header"""
    global config, log
    ua = (
        config["weather"]
        .get("useragent", "{ID}")
        .format(ID=config["weather"].get("ID"))
    )
    ua_str = f"{ua} ({config['weather'].get('contact')})"
    log.debug(f'curl -v {url} -H "User-Agent={ua_str}"')
    return requests.get(url=url, headers={"User-Agent": ua_str})

def linebreak(line:str) -> list[str]:
    lines = []
    if len(line) < 66:
        return [line]
    while len(line) > 66:
          line1 = line[:66]
          word_break = len(line1) - line1[::-1].index(" ")
          lines.append(line[:word_break-1])
          line = line[word_break:]
    lines.append(line)
    return lines

def get_weather() -> dict[str, Any]:
    """Fetch the weather info for the location."""
    global config, log
    log.info("Getting weather")

    # Date is part of the weather.
    now = datetime.datetime.now()
    result:dict[str, Any] = {
        "today": now.strftime("%A"),
        "date": now.strftime("%B %d, %Y %H:%M"),
    }
    update_hr = int(config["update"].get("period", "6")) or 6

    next_refresh_hr = ((now.hour // update_hr) * update_hr) + int(config["update"].get("offset_hour", "0"))
    if next_refresh_hr < now.hour:
        next_refresh_hr = next_refresh_hr + update_hr

    refresh_time = (now.replace(hour=next_refresh_hr, minute=0, second=0) - now).seconds

    # First, get the location info for where we want.
    base_url = config["weather"].get("url", "https://api.weather.gov")
    reply = req(
        f"{base_url}/points/{config['location'].get('latitude')},{config['location'].get('longitude')}"
    )
    reply.raise_for_status()
    point_data = reply.json()
    properties = point_data["properties"]

	# Sunrise / sunset and noon times.
    result["location"] = properties["relativeLocation"]["properties"]["city"]
    result["sunrise"] = datetime.datetime.fromisoformat(
        properties["astronomicalData"]["sunrise"]
    ).strftime("%H:%M %p")
    result["sunset"] = datetime.datetime.fromisoformat(
        properties["astronomicalData"]["sunset"]
    ).strftime("%I:%M %p")
    result["transit"] = datetime.datetime.fromisoformat(
        properties["astronomicalData"]["transit"]
    ).strftime("%H:%M %p")
    result["refresh"] = refresh_time

    # Get the hourly forecast...
    reply = req(properties.get("forecast"))
    reply.raise_for_status()
    fore_prop = reply.json().get("properties")
    result["forecast"] = []
    for i in range(3):
        period = fore_prop["periods"][i]
        forecast = {
            "name": period["name"],
            "temp": period["temperature"],
            "short": period["shortForecast"],
            "long": linebreak(period["detailedForecast"]),
        }
        result["forecast"].append(forecast)
    return result

class MQTT_server:

	def __init__(config, log):
          self.config = config
          self.log = log

	# The MQTT callbacks.
	def on_connect(client, _userdata, _flags, reason_code, properties):
		if reason_code.is_failure:
			self.log.error(f"Connection refused: {reason_code}")
			raise WeathervaneError(f"Connection rejected: {reason_code}")
		subscription = f"{config["mqtt"].get("topic")}/request"
		self.log.info(f"Subscribing to {subscription}")
		client.subscribe(subscription, qos=0)


	def on_subscribe(_client, _userdata, _mid, reason_code_list, _properties):
		if reason_code_list[0].is_failure:
			print("Connection rejected")
			raise WeathervaneError(f"Connection rejected: {reason_code_list[0]}")
		self.log.info(f"Subscribed, QoS {reason_code_list[0].value}")


	def on_unsubscribe(client, userdata, mid, reason_code_list, properties):
		self.log.info("Disconnecting...")
		client.disconnect()


	def on_message(client, userdata, message):
		self.log.debug(message.__class__)
		req = json.loads(message.payload)
		self.log.debug(f"message: {req}")
		report = get_weathe(userdata.settings)
		self.log.debug(f"Sending: {report}")
		client.publish(f"{self.config["mqtt"].get("topic")}/report", json.dumps(report))

class WVServer(BaseHTTPRequestHandler):

    def do_GET(self):
            report = json.dumps(get_weather())
            log.debug(report)

            self.send_response(200)
            self.send_header("Content-Type", "application/json");
            self.end_headers()
            self.wfile.write(report.encode())

# Make the magic happen.
def main():
	global config, log
	config = get_settings()

	log = get_logging(config.get("general", "logging"))
	server = HTTPServer(("0.0.0.0", port), WVServer)

	log.info(f"Starting server at {port}")

	try:
		server.serve_forever()
	except Exception as ex:
		log.error(f"Error: {ex}")
	finally:
		server.server_close()

	"""
    mq_serv = MQTT_server(log, config)
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = mq_serv.on_connect
    client.on_subscribe = mq_serv.on_subscribe
    client.on_unsubscribe = mq_serv.on_unsubscribe
    client.on_message = mq_serv.on_message
    if config["mqtt"].get("user") or config["mqtt"].get("pass"):
        client.username_pw_set(
            username=config["mqtt"].get("user"), password=config["mqtt"].get("pass")
        )

    client.connect(
        host=config["mqtt"].get("host"),
        port=int(config["mqtt"].get("port", 1883)),
    )
    client.loop_forever()
	"""

if __name__ == "__main__":
	try:
		main()
	except Exception as ex:
		print(ex)
