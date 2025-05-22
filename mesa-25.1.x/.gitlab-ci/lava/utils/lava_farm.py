import logging
import os


def get_lava_farm() -> str:
    """
    Returns the LAVA farm based on the FARM environment variable.

    :return: The LAVA farm
    """
    farm: str = os.getenv("FARM", "unknown")

    if farm == "unknown":
        logging.warning("FARM environment variable is not set, using unknown")

    return farm.lower()
