from setuptools import find_packages
from setuptools import setup

setup(
    name='techx_vision_bridge',
    version='2.0.0',
    packages=find_packages(
        include=('techx_vision_bridge', 'techx_vision_bridge.*')),
)
