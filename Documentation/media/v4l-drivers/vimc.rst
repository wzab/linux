The Virtual Media Controller Driver (vimc)
=========================================

This driver emulates video4linux hardware of varios media topologies. It exposes
media devices through /dev/mediaX notes, video capture devices through
/dev/videoX and sub-devices through /dev/v4l-subdevX.

A subdevice can be a sensor, a debayer or a scaler.

To configure a media device of a given topology, a ConfigFS API is provided.


Configuring the driver through ConfigFS (Experimental)
------------------------------------------------------

.. note::
This API is not finished yet and might change in the future.

Mount configfs:
::
	$ mkdir /configfs
	$ mount -t configfs none /configfs

When loading the module, you see a folders name vimc
::
	$ tree /configfs/
	/configfs/
	`-- vimc

1) Creating a media device
~~~~~~~~~~~~~~~~~~~~~~~~~~

To create a media device just create a new folder under /configfs/vimc/

Example:
::
	$ mkdir /configfs/vimc/mdev
	$ tree /configfs/vimc/mdev
	/configfs/vimc/mdev/
	|-- entities/
	|-- hotplug
	`-- links/

	2 directories, 1 file

2) Creating entities
~~~~~~~~~~~~~~~~~~~~

To create an entity in the media device's topology, just create a folder under
/configfs/vimc/<mdev-name>/entities/ with the following format:

	<sub-module>:<name>

Where <sub-module> is one of the following:
::
	vimc-sensor
	vimc-scaler
	vimc-debayer
	vimc-capture

Example:
::
	$ mkdir /configfs/vimc/mdev/entities/vimc-sensor:my-sensor
	$ mkdir /configfs/vimc/mdev/entities/vimc-capture:my-capture
	$ tree /configfs/
	/configfs/
	`-- vimc/
	    `-- mdev/
	        |-- entities/
	        |   |-- vimc-capture:my-capture/
	        |   |   `-- pad:sink:0/
	        |   `-- vimc-sensor:my-sensor/
	        |       `-- pad:source:0/
	        |-- hotplug
	        `-- links/

	8 directories, 1 file

3) Creating links
~~~~~~~~~~~~~~~~~

To create links between two entities in the topology, just create a folder under
/configfs/vimc/<mdev-name>/links/ with the following format:

	"<entity1-name><pad-source>:<entity2-name><pad-sink>"

Example:
::
	$ mkdir "/configfs/vimc/mdev/links/my-sensor:0->my-capture:0"
	$ tree /configfs
	/configfs/
	`-- vimc/
	    `-- mdev/
	        |-- entities/
	        |   |-- vimc-capture:my-capture/
	        |   |   `-- pad:sink:0/
	        |   `-- vimc-sensor:my-sensor/
	        |       `-- pad:source:0/
	        |-- hotplug
	        `-- links/
	            `-- my-sensor:0->my-capture:0/
	                `-- flags

	9 directories, 2 files

Change the attributes of the link by writing in the file
"/configfs/vimc/<mdev-name>/links/<my-link>/flags"

Flag values are defined in :ref:`include/uapi/linux/media.h <media_header>`
( seek for ``MEDIA_LNK_FL_*``)

1 - Enabled
	Indicates the link will be enabled when the media device is created.

3 - Enabled and Immutable
	Indicates that the link enabled state can't be modified at runtime.

Example:
::
	$ echo 3 > "/configfs/vimc/mdev/links/my-sensor:0->my-capture:0/flags"

4) Activating/Deactivating device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To activate the device, write one of "plugged", "plug" or "1" to file
/configfs/vimc/<ndev-name>/hotplug

Example:
::
	$ echo 1 > /configfs/vimc/mdev/hotplug

You should see a new node /dev/mediaX in your devfs.

To deactivate the device, write one of "unplugged", "unplug" or "0" to file
/configfs/vimc/<ndev-name>/hotplug

Example:
::
	$ echo 0 > /configfs/vimc/mdev/hotplug

Subdevices
----------

Subdevices defines the behavior of an entity in the topology. Depending on the
subdevice, the entity can have multiple pads of type source or sink.

vimc-sensor:
	Generates images in several formats using video test pattern generator.
	Exposes:

	* 1 Pad source

vimc-debayer:
	Transforms images in bayer format into a non bayer format.
	Exposes:

	* 1 Pad sink
	* 1 Pad source

vimc-scaler:
	Mutiplies the size of the image by 3.
	Exposes:

	* 1 Pad sink
	* 1 Pad source

vimc-capture:
	Exposes node /dev/videoX to allow userspace to capture the stream.
	Exposes:

	* 1 Pad sink
	* 1 Pad source
