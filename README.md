Findsat
=======

Findsat is a spectrum analyser for RTL2832 tuners based on "rtlizer" from  Alexandru Csete. 
The original code has been extended to include spectral averaging and some peak detection 
algorithms. 

At present this software operates in the following modes:  
mode 0:   simple spectrum analyser 
mode 1:   detect spectral peak at specified frequency
mode 2:   try to detect LEO satellite at specific frequency
mode 3:   as for mode 2, but cycle through several satellites   

In modes 1 to 3, 'detections' are logged to a text file.   In modes 2 and 3, a heuristic 
algorithm is used that tries to identify LEO satellites, based on their decreasing Doppler 
frequency offset.  The present approach can be much improved!   Some sample results can 
be seen at https://sites.google.com/site/rtltuners/spectrum-analyser

Dependencies   (as for rtlizer) 
------------

* [rtlsdr](http://sdr.osmocom.org/trac/wiki/rtl-sdr)
* [Gtk+ v2](http://www.gtk.org/)
* [kiss_fft](http://kissfft.sourceforge.net/) (included)

Build
-----

Build on i386-like linux PCs with the simple build script. 


Usage
=====

Start this application with:   ./findsat [centre_frequency_MHz]
The window size is fixed at 800x400 pixels.  Some user controls are available 
through single-letter keyboard input.  At present:  

up arrow:    increase the sampling rate (Fs) 
down arrow   decrease the sampling rate 
right arrow  increase the centre frequency (by Fs/2)
left arrow   decrease the centre frequency 

s  cycle through the 4 modes mentioned above 
a  toggle AGC mode of RTL tuner 
G  increase tuner gain (only applies in manual gain mode)
g  decrease tuner gain (only applies in manual gain mode)
>  increase the power detection threshold (by 0.5 dB) 
<  decrease the power detection threshold (by 0.5 dB) 
S  cycle through "target" list, (in modes 1 and 2) 


Credits
-------

Findsat is under development by Bill Cowley, based on 
Rtlizer was written by Alexandru Csete.
Uses code from rtl_test by Steve Markgraf.
Includes kiss_fft by Mark Borgerding.
