
# OTA template

Template for ESP32 apps that can be updated over the air

The binaries for the app and for the spiffs embedded webpages (if any) are hosted on a https sever
The folder containg the binairies must als contain textfiles with the version number of the (new) software.
These files (firmWareVersion.txt for the app and storageVersion.txt for the website) are read.
If the version differ from the present version:
In case of the spiffs: The new image is flashed into the spiffs partition
Use partionsOTA_4M ( or 8M).csv.

 openssl s_client -showcerts -connect www.digkleppe.nl:443 </dev/null

   /* Root cert for howsmyssl.com, taken from server_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
 
      





