Assignment #3: Ray tracing
FULL NAME: Sean Saleh

MANDATORY FEATURES
------------------
<Under "Status" please indicate whether it has been implemented and is
functioning correctly.  If not, please explain the current status.>

Feature:                                 Status: finish? (yes/no)
-------------------------------------    -------------------------
1) Ray tracing triangles                            yes
2) Ray tracing sphere                               yes
3) Triangle Phong Shading                           yes
4) Sphere Phong Shading                             yes
5) Shadows rays                                     yes
6) Still images                                     yes
   
7) Extra Credit (up to 20 points)  

A) Anti-aliasing:	The images are antialiased: each pixel asts 4 rays and averages the output from them

B) Multi-threaded:	The application is multi-theaded. Since this is fundamentally a highly parallizable problem set I decided to multithread it
	Implementation details:	Multi-threading uses OpenMP, since it is cross platform and included in VS c++
							A lot of the base code had to be rewrittenin order to run both with and without OpenMP compiler support
							I decided to have a live preview of the rendering that clearly indicates how many threads are working, thus you see the image render in chunks
							The number of threads used should be (number of threads available on system) - 1 (for displaying the preview)
							Timing wise it is more efficient than non multi-threaded. However, some efficieny is lost through the live preview. If you want faster results change 
	Run instructions:	Compile with OpenMP enabled. Visual Studio compiler switch: /openmp