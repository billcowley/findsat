/* findsat : find satellites by looking for decreasing spectral peak with RTL tuners 
 * 
 *
 * This code is a modified version of rtlizer - a simple spectrum analyzer using rtlsdr.
 * The main additions are:  spectral averaging;  slightly more graphics;  extra modes
 * for detecting signals based on their power spectrum and whether its peak changes 
 * Bill Cowley 2013, 2014 
 *  
 * Copyright (C) 2013 Alexandru Csete (for rtlizer code)  
 * Includes code from rtl_test.c:
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * findsat and rtlizer are free software: you can redistribute and/or modify
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or later.  
 *
 */
#include <cairo.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <rtl-sdr.h>
#include <stdlib.h>
#include <time.h>
#include "kiss_fft.h"


#define DEFAULT_SAMPLE_RATE 1000000   //2600000
#define DYNAMIC_RANGE 90.f  /* -dBFS coreresponding to bottom of screen */
#define SCREEN_FRAC 0.8f  /* fraction of screen height used for FFT */
#define NAV  40 
#define PI   3.14159

uint8_t *buffer;
uint32_t dev_index = 0;
uint32_t frequency = 432500000;
uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
uint32_t buff_len = 2048; 
float    Fsat = 432.45e6; // default target frequency 
int      satnum =1;       // default satellite 
int      gain   = 340;     // 34dB default

int      gains[] = {15, 40, 65, 90, 115, 140, 190, 215, 240, 290, 340, 420, 480}; 
int      gaini   = 9;      // default gain setting 
int      xi, xi1, xi2;       
int      lastpeakindex=0; 
int      searchwid = 15;   // half full search range in bins 
FILE     *log_file;

/*      
	Fsat = 437.425e6;  //AAusat-3 
    	Fsat = 437.822e6; //IO-26
	Fsat = 437.47e6;   //CO-55 down 
	Fsat = 436.8375e6;   //CO-55 beacon 
	Fsat = 437.49e6;   //CO-57 down 
	Fsat = 436.8475e6;   //CO-57 beacon 
	Fsat = 437.345e6;  //  CO-58 downlink 
        Fsat = 437.365e6;   //CO-58 beacon 
        Fsat = 437.150e6;     //LO-19 downlink  
        Fsat = 437.125e6;   //LO-19 Beacon  
	// reset to VK5VF beacon 
	satnum=0; Fsat = 432.45e6;  } */

float    satfreq[] ={432.45e6, 
		     437.425e6, 437.822e6, 437.47e6, 436.8375e6, 
		     437.49e6,  436.8475e6, 437.345e6,  437.365e6,  
		     437.150e6, 437.125e6}; 
int      score = 0;       
int      satsearch =0;  

// score >0:  look for reducing spectral peak over (say) 100 PSEs 
//            if not found then change satellite


int fft_size = 1024; // 320;
kiss_fft_cfg  fft_cfg;
kiss_fft_cpx *fft_in;
kiss_fft_cpx *fft_out;
float         *log_pwr_fft; /* dbFS relative to 1.0 */
float         *pvec;   
static float  power_thres = 6.0;   // dB  
float scale, fc, sig_rms;
int yzero = 0;
int text_margin = 0;
int iflag =0;  
int agc_mode=0;  

static rtlsdr_dev_t *dev = NULL;
static gint width, height; /* screen width and height */
static gboolean freq_changed = TRUE;


static gboolean delete_event(GtkWidget *widget, GdkEvent *e, gpointer d)
{
  return FALSE;
}

static void destroy(GtkWidget *widget, gpointer data)
{
  rtlsdr_close(dev);
  free(buffer);
    
  free(fft_cfg);
  free(fft_in);
  free(fft_out);
  free(log_pwr_fft);

  gtk_main_quit();
}

gint keypress_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
  guint event_handled = TRUE;
  int r;
	
  switch (event->key.keyval)
    {
    case GDK_KEY_Return:
      /* exit application */
      gtk_widget_destroy(widget);
      break;
    case GDK_KEY_s:   //    toggle satellite search mode 
      satsearch = (satsearch+1) %4; 
      break; 
    case GDK_KEY_a:   //    toggle AGC mode
      if (agc_mode) agc_mode = 0; else agc_mode = 1;
      if (agc_mode)     {
	r = rtlsdr_set_tuner_gain_mode(dev, 0);
	if (r < 0)
	  fprintf(stderr, "WARNING: Failed to enable  auto gain.\n");
      }
      else   {
	r = rtlsdr_set_tuner_gain_mode(dev, 1);
	if (r < 0)
	  fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
	// it seems necessary to reset gain after turning off AGC 
	r = rtlsdr_set_tuner_gain(dev, gain);
	if (r < 0)
	  fprintf(stderr, "WARNING: Failed to set manual gain.\n");
      }
      fprintf(stderr, "AGC mode is now %d\n", agc_mode);
      break;   
    case GDK_KEY_Left:
      /* decrease frequency */
      frequency -= samp_rate/4;
      r = rtlsdr_set_center_freq(dev, frequency);
      if (r < 0)
	fprintf(stderr, "WARNING: Failed to set center freq.\n");
      break;

    case GDK_KEY_Right:
      /* increase frequency */
      frequency += samp_rate/4;
      r = rtlsdr_set_center_freq(dev, frequency);
      if (r < 0)
	fprintf(stderr, "WARNING: Failed to set center freq.\n");
      break;
       

    case GDK_KEY_Up:
      /* increase bandwidth with 200 kHz */
      if (samp_rate < 2400000)
        {
	  samp_rate += 200000;
	  r = rtlsdr_set_sample_rate(dev, samp_rate);
	  if (r < 0)
	    fprintf(stderr, "WARNING: Failed to set sample rate.\n");
        }
      break;

    case GDK_KEY_Down:
      /* decrease bandwidth with 100 kHz */
      if (samp_rate > 1000000)
        {
	  samp_rate -= 200000;
	  r = rtlsdr_set_sample_rate(dev, samp_rate);
	  if (r < 0)
	    fprintf(stderr, "WARNING: Failed to set sample rate.\n");
	  //r=  rtlsdr_get_sample_rate(dev);
	  //fprintf(stderr, "Reported sample rate is %d Hz\n", r);
	  // sample rates < 1e6 don't seem to work -- but no error is 
          // reported ??? 
        }
      break;
    case GDK_KEY_less:
      if (power_thres > 0)
        {
	  power_thres -= 0.5;
	  fprintf(stderr, "reduced power thres to %f \n", power_thres);
        }
      break;
    case GDK_KEY_greater:
      if (power_thres <50)
        {
	  power_thres += 0.5;
	  fprintf(stderr, "increased power thres to %f\n ", power_thres); 
        }
      break;
        
	
    case GDK_KEY_S:         // change target freq 
      satnum = satnum + 1;  
      if (satnum>10) satnum=0;
      Fsat = satfreq[satnum]; 

         
      fprintf(stderr, "Fsat is %d   %8.3f MHz\n ", satnum, Fsat/1e6); 
      break;

    case GDK_KEY_G:      /* increase tuner gain */
      if (gaini<12) {
	gaini += 1;  gain = gains[gaini]; 
      }
      r = rtlsdr_set_tuner_gain(dev, gain);
      if (r < 0)
	fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
      else
	fprintf(stderr, "Tuner gain set to %f dB.\n", gain/10.0);
      break; 

    case GDK_KEY_g:      /* decrease tuner gain */
      if (gaini>0)   {
	gaini -= 1;  gain = gains[gaini]; 
      }
      r = rtlsdr_set_tuner_gain(dev, gain);
      if (r < 0)
	fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
      else
	fprintf(stderr, "Tuner gain set to %f dB.\n", gain/10.0);
      // fprintf(stderr, "RMS signal is  %f .\n", sig_rms);
      break; 

    default:
      event_handled = FALSE;
      iflag =1;  
      break;
    }

  return event_handled;
}

static int db_to_pixel(float dbfs)
{
  return yzero+(int)(-dbfs*scale);
}

static void draw_text(cairo_t *cr)
{
  cairo_text_extents_t cte;
  double txt1_y, txt2_y;
  int    i;  

  gchar *freq_str = g_strdup_printf("%.3f MHz", 1.e-6f*(float)frequency);
  gchar *delta_str = 
        g_strdup_printf("BW: %.1f MHz RBW: %.2f kHz Mode %d RMS %4.1f",
			1.e-6f*(float)samp_rate,
			1.e-3f*(float)(samp_rate/fft_size),
			satsearch, sig_rms);
  /* clear area */
  cairo_set_source_rgb(cr, 0.02, 0.02, 0.09);
  cairo_set_line_width(cr, 1.0);
  cairo_rectangle(cr, 0, 0, width, yzero);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);

  cairo_select_font_face(cr, "Sans",
			 CAIRO_FONT_SLANT_NORMAL,
			 CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 24);
  cairo_text_extents(cr, freq_str, &cte);
  txt1_y = text_margin + cte.height;
  cairo_set_source_rgba(cr, 0.97, 0.98, 0.02, 0.8);
  cairo_move_to(cr, text_margin, txt1_y);
  cairo_show_text(cr, freq_str);

  cairo_set_font_size(cr, 12);
  cairo_text_extents(cr, delta_str, &cte);
  txt2_y = txt1_y + cte.height + text_margin;
  cairo_set_source_rgba(cr, 0.97, 0.98, 0.02, 0.8);
  cairo_move_to(cr, text_margin, txt2_y);
  cairo_show_text(cr, delta_str);

 
 
  cairo_set_font_size(cr, 10);
  cairo_text_extents(cr, delta_str, &cte);
  // txt3_y = txt2_y + cte.height + text_margin;
  cairo_set_source_rgba(cr, 0.5, 0.98, 0.02, 0.8);
  for (i=0; i<6; i++)  {
    cairo_move_to(cr, i*width/4-10, 130); 
    gchar *flab_str  = g_strdup_printf("%.2f", 
              1.e-6f*(float)(frequency+samp_rate*((float)(i-2))/4.0));
    cairo_show_text(cr, flab_str);
    g_free(flab_str); 
  }

  g_free(freq_str);
  g_free(delta_str);
 
}

static void draw_fft(cairo_t *cr)
{
  cairo_set_source_rgb(cr, 0.02, 0.02, 0.09);
  cairo_set_line_width(cr, 1.0);

  cairo_rectangle(cr, 0, yzero, width, height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);

  //cairo_set_source_rgba(cr, 0.49, 0.50, 0.63, 0.85);
  cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.9);

  int x, y;
  for (x = 0; x < width; x++ )
    {
      y = db_to_pixel(log_pwr_fft[x]);
      // g_random_int_range(10, 100);   /// what does this do ?? 
      cairo_move_to(cr, x, height);
      cairo_line_to(cr, x, y);   
    }

  if (satsearch>0)  {
    //if (xi2-xi1==1)  cairo_set_source_rgba(cr, 0.7, 0.0, 0.0, 0.9);

    cairo_move_to(cr, xi1, 135); cairo_line_to(cr, xi1, 145);
    cairo_move_to(cr, xi2, 135); cairo_line_to(cr, xi2, 145);
  }
  //if (xi2-xi1==1)  cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 0.9);
   

  for (x=0; x<6; x++)  {
    cairo_move_to(cr, x*width/4, 110);
    // cairo_line_to(cr, x*width/10, 160);
    //if (x%2==0)  cairo_line_to(cr, x*width/5, 130);
    //else  
    cairo_line_to(cr, x*width/4, 120);    
  }

  cairo_stroke(cr);    
}

static void setup_rtlsdr()
{
  int device_count;
  int r;

  buffer = malloc(buff_len * sizeof(uint8_t));

  device_count = rtlsdr_get_device_count();
  if (!device_count)
    {
      fprintf(stderr, "No supported devices found.\n");
      exit(1);
    }

  r = rtlsdr_open(&dev, dev_index);
  if (r < 0)
    {
      fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
      exit(1);
    }

  r = rtlsdr_set_sample_rate(dev, samp_rate);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set sample rate.\n");

  r = rtlsdr_set_center_freq(dev, frequency);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set center freq.\n");

  /* r = rtlsdr_set_tuner_gain_mode(dev, 0);
     if (r < 0)
     fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
  */
     
  r = rtlsdr_set_tuner_gain_mode(dev, 1);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to enable manual gain.\n");

  /* Set the tuner gain */
  r = rtlsdr_set_tuner_gain(dev, gain);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
  else
    fprintf(stderr, "Tuner gain set to %f dB.\n", gain/10.0);

  r = rtlsdr_reset_buffer(dev);
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to reset buffers.\n");

  // added 14/9/13 
  r= rtlsdr_set_freq_correction(dev, 90); 
  if (r < 0)
    fprintf(stderr, "WARNING: Failed to set freq correction.\n");
    
}

static gboolean read_rtlsdr()
{
  gboolean error = FALSE;
  int n_read;
  int r;

  r = rtlsdr_read_sync(dev, buffer, buff_len, &n_read);
  if (r < 0) {
    fprintf(stderr, "WARNING: sync read failed.\n");
    error = TRUE;
  }

  if ((uint32_t)n_read < buff_len) {
    fprintf(stderr, "Short read (%d / %d), samples lost, exiting!\n", n_read, buff_len);
    error = TRUE;
  }

  return error;
}

static int run_fft()
{   
  int i;    
  int maxi=0; 
  int k; 
  kiss_fft_cpx pt;
  float max_dB, min_dB, sigma2; 

  for (i=0; i<fft_size; i++)  pvec[i] = 0.0; 
  sigma2 = 0;  

  for (k=0; k<NAV; k++)  
    { 

      /* get samples from rtlsdr */
      if (read_rtlsdr())
	return FALSE;  /* error reading -> exit */
    
      /* calculate FFT */
      // see struct def in kiss_fft.h; 255.f forces 255 to be float 

      for (i = 0; i < fft_size; i++)
	{
	  fft_in[i].r = ((float)buffer[2*i])/255.f - 0.5;
	  fft_in[i].i = ((float)buffer[2*i+1])/255.f -0.5;

          sigma2 = sigma2 + fft_in[i].r*fft_in[i].r*16384.0 
	    +  fft_in[i].i*fft_in[i].i*16384.0;  

	  // try window -- doesn't do much ?  
	  //fft_in[i].r *= (1-cos(PI*(float)i/(fft_size-1))); 
	  //fft_in[i].i *= (1-cos(PI*(float)i/(fft_size-1))); 

	}

      kiss_fft(fft_cfg, fft_in, fft_out);
      for (i = 0; i < fft_size; i++)
	{
	  /* shift, normalize and convert to dBFS */
	  if (i < fft_size / 2)
	    {
	      pt.r = fft_out[fft_size/2+i].r / fft_size;
	      pt.i = fft_out[fft_size/2+i].i / fft_size;
	    }
	  else
	    {
	      pt.r = fft_out[i-fft_size/2].r / fft_size;
	      pt.i = fft_out[i-fft_size/2].i / fft_size;
	    }
	  pvec[i]  = pvec[i] + pt.r * pt.r + pt.i * pt.i;
	}
    }
  sig_rms = sqrt(sigma2/(2.0*(float)fft_size*(float)NAV)); 
  min_dB = 1000;  max_dB = -1000; 
  for (i = 0; i < fft_size; i++)   {
    log_pwr_fft[i] = 10.f * log10(pvec[i] + 1.0e-20f);
  }

  float peak_freq; 
  peak_freq = (float)frequency;   // start with carrier freq; determine range
  xi= (int)((Fsat - (peak_freq-samp_rate/2.0)) /((float)samp_rate/fft_size));  
   
  if (satsearch>0)   {
    xi1 = xi - searchwid;    xi2 = xi+searchwid;   
    if (xi1<1)    xi1 = 1; 
    if (xi2<xi1)  xi2 = xi1 + 1;  
    if (xi2>fft_size-1) xi2 = fft_size-1;
    if (xi1>xi2)        xi1 = xi2-1;
  }
  else {xi1=1;   xi2 = fft_size-1; }
  if (iflag>0)   {         
    g_print("target index range %d to %d \n ", xi1, xi2);
    g_print("PSD from %.3f to %.3f MHz\n", 
	    (float)(peak_freq-samp_rate/2)/1e6,  (float)(peak_freq+samp_rate/2)/1e6); 
    iflag=0; 
  }

  for (i = xi1; i < xi2; i++)   {
    if (min_dB>log_pwr_fft[i])  min_dB = log_pwr_fft[i]; 
    if (max_dB<log_pwr_fft[i])  {max_dB = log_pwr_fft[i]; maxi = i;}
  }
  float pdiff;  pdiff = max_dB - min_dB;   

  if (satsearch>0) {
    if (pdiff>power_thres)  {  
      time_t current_time;
      char* c_time_string;
      current_time = time(NULL);
      c_time_string = ctime(&current_time);
   
      peak_freq += (float)(maxi-(int)fft_size/2)*(float)(samp_rate)/(float)(fft_size); 
      peak_freq = peak_freq / 1000.0; 
      g_print("peak: %d %d ind %d at %7.3f MHz, mag %5.1f dB, \r", 
	      score, satnum, maxi, peak_freq/1e3, pdiff);
      if (satsearch>1)  {
	if (maxi==lastpeakindex-1) {  
	  score = score / 2;    // reward falling peak location 
	  g_print("sat peak: %d %d ind %d at %7.3f MHz, mag %5.1f dB, %s", 
		  score, satnum, maxi, peak_freq/1e3, pdiff, c_time_string);
	  fprintf(log_file, "sat peak: %3d %2d ind %4d at %7.3f MHz, mag %5.1f dB, %s", 
		  score, satnum, maxi, peak_freq/1e3, pdiff, c_time_string);
	  fflush(log_file); 
	}
	else  if (maxi==lastpeakindex+1) score = score * 3; // penalise inc peak freq 
	else  score +=1;  // small penalty 
	lastpeakindex = maxi;  
      }
      if (satsearch==1)        {   // if pdiff large enough and mode 1 
	g_print("peak: target %d  at %7.3f MHz, mag %5.1f dB, %s\a", 
		satnum, peak_freq/1e3, pdiff, c_time_string);
	fprintf(log_file, "peak: target %d  at %7.3f MHz, mag %5.1f dB, %s",
		satnum, peak_freq/1e3, pdiff, c_time_string);
	fflush(log_file); 
      }
    }
    else score +=10; // case of no peak found 

    if (xi2-xi1>1)
    g_print("mode %d score %d target # %d                                    \r", 
	    satsearch, score, satnum); else 
    g_print("mode %d target %d frequency is outside bandwidth                \r", 
            satsearch, satnum);     
  }
    
  if (score>100) {
    score = 0; 
    if (satsearch>2) {
      satnum=satnum+1; 
      if (satnum>10) satnum=1;
    } 
    Fsat = satfreq[satnum]; 
  }    
  return TRUE;  
}

gint timeout_cb(gpointer darea)
{
  /* get samples from rtlsdr */
  // if (read_rtlsdr())
  // return FALSE;  /* error reading -> exit */
    
  /* calculate FFT */
  run_fft();
	
  /* update plot */
  cairo_t *cr;
  cr = gdk_cairo_create(gtk_widget_get_window(GTK_WIDGET(darea)));
  draw_fft(cr);
  if (freq_changed)
    {
      draw_text(cr);
      //freq_changed = FALSE;
    }
  cairo_destroy(cr);
	
  return TRUE;
}

int main(int argc, char *argv[])
{

  fprintf(stderr, "Starting satfind\n");
  log_file =fopen("satsearch.log", "w");
  if (!log_file)  fprintf(stderr, "Failed to open log file\n"); 

  /* GtkWidget is the storage type for widgets */
  GtkWidget *window;
  guint  tid;
    
  gtk_init (&argc, &argv);
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);

  g_signal_connect(window, "delete-event", G_CALLBACK (delete_event), NULL);
  g_signal_connect(window, "destroy", G_CALLBACK (destroy), NULL);
  g_signal_connect(window, "key_press_event", G_CALLBACK (keypress_cb), NULL);

  /* use default window size as no geometry is specified */
  width = 800; // 320;  //gdk_screen_width();
  height = 400; //gdk_screen_height();
  /* if (argc > 1)
    {
      if (!gtk_window_parse_geometry(GTK_WINDOW(window), argv[1]))
	fprintf(stderr, "Failed to parse '%s'\n", argv[1]);
      else
	gtk_window_get_default_size(GTK_WINDOW(window), &width, &height);
	}   */ 
  if (argc>1) {
    sscanf(argv[1],"%f",&fc); 
    fprintf(stderr, "Setting Center Frequency to %f MHz\n",fc );
    frequency = (uint32_t) (1.0e6*fc);
  } 

  gtk_window_set_default_size(GTK_WINDOW(window), width, height);
  scale = (float)height/DYNAMIC_RANGE * SCREEN_FRAC;
  yzero = (int)(height*(1.0f-SCREEN_FRAC));
  text_margin = yzero/10;

  g_print("window size: %dx%d pixels\n", width, height);
  g_print("SCALE: %.2f / Y0: %d / TXTMARG: %d\n", scale, yzero, text_margin);

  gtk_widget_show(window);
  gdk_window_set_cursor(gtk_widget_get_window(window), gdk_cursor_new(GDK_BLANK_CURSOR));

  /* set up FFT */
  fft_size = 2 * width/2;
  fft_cfg = kiss_fft_alloc(fft_size, FALSE, NULL, NULL);
  fft_in = malloc(width * sizeof(kiss_fft_cpx));
  fft_out = malloc(width * sizeof(kiss_fft_cpx));
  log_pwr_fft = malloc(width * sizeof(float));
  pvec = malloc(width * sizeof(float));

  setup_rtlsdr();

  tid = g_timeout_add(800, timeout_cb, window);
    
  gtk_main();
    
  g_source_remove(tid);
    
  return 0;
}

