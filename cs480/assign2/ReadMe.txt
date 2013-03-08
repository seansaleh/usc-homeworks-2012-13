ASSIGN2(1)						User Manuals					  ASSIGN2(1)

NAME
    assign2 - Assignment 2 for CS480 Graphics by Sean Saleh

SYNOPSIS
    assign2.exe filename
	
DESCRIPTION
    ASSIGN2 Displays a rollercoaster in a full 3d environment. Allows the
    user to ride said rollercoaster. Can load in various track or spline
    files to allow the user to customize a track.

CONTROLS
    Press 'w': Moves forward along the track
    Press 's': Moves backwards along the track

    Select "Quit" from right click menu: Quits the application
    Select "Capture Animation" from right click menu: Records an animation

    Note: These controls are left in to explor the world, the primary use-
          case recommends that you do not use them
		  
          Left click or middle click and drag:  rotates the rendered scene
    Ctrl- Left click or middle click and drag:  translates the redered scene
    Shift-Left click or middle click and drag:  scales the scene

FILES
    filename
        Specified as an argument as which file to open.  Must be a trackfile
		with the format like track.txt (Given example included with code)

IMPLEMENTATION NOTES
    Skybox: Used 5 polygons around the top and sides of the world. Textured
            them individually with a texture found free at hazelwhorley.com
    Coaster Normals: Took the tangent of the line at any point, then found N
            as unit(T x arbitrary vector), then B as unit(T x N). Used these
            values to build a box at each u differentiation.

BUGS
    No current known bugs. Please report any to the author.

EXTRA CREDIT
    Can Autoride rollercoaster with selecting from right click menu
        Note: may be slower since its also taking screenshots
    Finished Assignment!
        This extra credit can be awarded by simply giving full points
    
AUTHOR
    Sean Saleh <sean.saleh@usc.edu>