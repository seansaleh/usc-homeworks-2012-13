// assign2.cpp : Defines the entry point for the console application.
//

/*
	CSCI 480 Computer Graphics
	Assignment 2: Simulating a Roller Coaster
	C++ starter code
*/

#include "stdafx.h"
#include <pic.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <GL/glu.h>
#include <GL/glut.h>
#include <math.h>

/* For menu and map control */
int g_iMenuId;

int g_vMousePos[2] = {0, 0};
int g_iLeftMouseButton = 0;    /* 1 if pressed, 0 if not */
int g_iMiddleMouseButton = 0;
int g_iRightMouseButton = 0;
typedef enum { ROTATE, TRANSLATE, SCALE} CONTROLSTATE;
CONTROLSTATE g_ControlState = ROTATE;

//Control variables for recording animation
bool recordingAndAnimating = false;
int currentRecordingFrame = 0;

/* state of the world */
float g_vLandRotate[3] = {0.0, 0.0, 0.0};
float g_vLandTranslate[3] = {0.0, 0.0, 0.0};
float g_vLandScale[3] = {1.0, 1.0, 1.0};

/* represents one control point along the spline */
struct point {
	double x;
	double y;
	double z;
};

/* spline struct which contains how many control points, and an array of control points */
struct spline {
	int numControlPoints;
	struct point *points;
};

/* the spline array */
struct spline *g_Splines;

/* total number of splines */
int g_iNumOfSplines;

int loadSplines(char *argv) {
	char *cName = (char *)malloc(128 * sizeof(char));
	FILE *fileList;
	FILE *fileSpline;
	int iType, i = 0, j, iLength;

	/* load the track file */
	fileList = fopen(argv, "r");
	if (fileList == NULL) {
		printf ("can't open file\n");
		exit(1);
	}
  
	/* stores the number of splines in a global variable */
	fscanf(fileList, "%d", &g_iNumOfSplines);

	g_Splines = (struct spline *)malloc(g_iNumOfSplines * sizeof(struct spline));

	/* reads through the spline files */
	for (j = 0; j < g_iNumOfSplines; j++) {
		i = 0;
		fscanf(fileList, "%s", cName);
		fileSpline = fopen(cName, "r");

		if (fileSpline == NULL) {
			printf ("can't open file\n");
			exit(1);
		}

		/* gets length for spline file */
		fscanf(fileSpline, "%d %d", &iLength, &iType);

		/* allocate memory for all the points */
		g_Splines[j].points = (struct point *)malloc(iLength * sizeof(struct point));
		g_Splines[j].numControlPoints = iLength;

		/* saves the data to the struct */
		while (fscanf(fileSpline, "%lf %lf %lf", 
			&g_Splines[j].points[i].x, 
			&g_Splines[j].points[i].y, 
			&g_Splines[j].points[i].z) != EOF) {
			i++;
		}
	}

	free(cName);

	return 0;
}

/**********************************************************************************************/
/* Application specific code */
/**********************************************************************************************/

//For Debug purposes
void renderPlane()
{
	glBegin(GL_POLYGON);

	glColor3f(1.0, 1.0, 1.0);
	glVertex3f(0.0, 0.0, 0.0);
	glColor3f(0.0, 0.0, 1.0);
	glVertex3f(0.0, 300.0, 0.0);
	glColor3f(0.0, 0.0, 0.0);
	glVertex3f(300.0, 300.0, 0.0);
	glColor3f(1.0, 1.0, 0.0);
	glVertex3f(300.0, 0.0, 0.0);

	glEnd();
}

/**********************************************************************************************/
/* General GL and application code */
/**********************************************************************************************/

/* Write a screenshot to the specified filename */
void saveScreenshot (char *filename)
{
	int i/*, j*/;
	Pic *in = NULL;

	if (filename == NULL)
		return;

	/* Allocate a picture buffer */
	in = pic_alloc(640, 480, 3, NULL);

	printf("File to save to: %s\n", filename);

	for (i=479; i>=0; i--) {
		glReadPixels(0, 479-i, 640, 1, GL_RGB, GL_UNSIGNED_BYTE,
			&in->pix[i*in->nx*in->bpp]);
	}

	if (jpeg_write(filename, in))
		printf("File saved Successfully\n");
	else
		printf("Error in Saving\n");

	pic_free(in);
}

/* converts mouse drags into information about 
rotation/translation/scaling */
void mousedrag(int x, int y)
{
	int vMouseDelta[2] = {x-g_vMousePos[0], y-g_vMousePos[1]};

	switch (g_ControlState)
	{
	case TRANSLATE: //Control
		if (g_iLeftMouseButton)
		{
			g_vLandTranslate[0] += vMouseDelta[0]/**0.01*/;
			g_vLandTranslate[1] -= vMouseDelta[1]/**0.01*/;
		}
		if (g_iMiddleMouseButton)
		{
			g_vLandTranslate[2] += vMouseDelta[1]/**0.01*/;
		}
		break;
	case ROTATE:
		if (g_iLeftMouseButton)
		{
			g_vLandRotate[0] += vMouseDelta[1];
			g_vLandRotate[1] += vMouseDelta[0];
		}
		if (g_iMiddleMouseButton)
		{
			g_vLandRotate[2] += vMouseDelta[1];
		}
		break;
	case SCALE: //Shift
		if (g_iLeftMouseButton)
		{
			g_vLandScale[0] *= 1.0+vMouseDelta[0]*0.01;
			g_vLandScale[1] *= 1.0-vMouseDelta[1]*0.01;
		}
		if (g_iMiddleMouseButton)
		{
			g_vLandScale[2] *= 1.0-vMouseDelta[1]*0.01;
		}
		break;
	}
	g_vMousePos[0] = x;
	g_vMousePos[1] = y;
}

void mouseidle(int x, int y)
{
	g_vMousePos[0] = x;
	g_vMousePos[1] = y;
}

void mousebutton(int button, int state, int x, int y)
{

	switch (button)
	{
	case GLUT_LEFT_BUTTON:
		g_iLeftMouseButton = (state==GLUT_DOWN);
		break;
	case GLUT_MIDDLE_BUTTON:
		g_iMiddleMouseButton = (state==GLUT_DOWN);
		break;
	case GLUT_RIGHT_BUTTON:
		g_iRightMouseButton = (state==GLUT_DOWN);
		break;
	}

	switch(glutGetModifiers())
	{
	case GLUT_ACTIVE_CTRL:
		g_ControlState = TRANSLATE;
		break;
	case GLUT_ACTIVE_SHIFT:
		g_ControlState = SCALE;
		break;
	default:
		g_ControlState = ROTATE;
		break;
	}

	g_vMousePos[0] = x;
	g_vMousePos[1] = y;
}

void menufunc(int value)
{
	switch (value)
	{
	case 0:
		exit(0);
		break;
	case 4:
		recordingAndAnimating = !recordingAndAnimating;
		break;
	default:
		break;
	}
}

void glInit()
{
	/* setup gl view here */
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glEnable(GL_DEPTH_TEST);
}

void reshape(int width, int height)
{
	//Set viewport
	glViewport(0,0,width,height);

	//Change mode
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	//Set persepective at 60degrees
	gluPerspective(60.0,1.0*width/height, 0.1, 10000.0);

	//Go back to normal mode just in case
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void doIdle()
{
	/* do some stuff... */
	if (recordingAndAnimating) //If the menu item to record and animate has been called
	{
		//Record current Frame
		
		char name[10];
		_snprintf_s(name, 10, "%03d.jpg", currentRecordingFrame);
		saveScreenshot(name);
		currentRecordingFrame++;
		
		//Rotate
		g_vLandRotate[1]+=2;

	}
	
	/* make the screen update */
	glutPostRedisplay();
}

void display()
{
	//Clear buffers and setup for drawing
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	//Setup Camera
	gluLookAt(0,200,200, 0,0,0,0,1,0);

	//Rotate
	glRotatef(g_vLandRotate[0],1.0,0.0,0.0);
	glRotatef(g_vLandRotate[1],0.0,1.0,0.0);
	glRotatef(g_vLandRotate[2],0.0,0.0,1.0);
	//Scale
	glScalef(g_vLandScale[0],g_vLandScale[1],g_vLandScale[2]);
	//Translate
	glTranslatef(g_vLandTranslate[0],g_vLandTranslate[1],g_vLandTranslate[2]);

	//Draw after this
	renderPlane(); //Is for debug
	//renderMap();

	//Swap buffer since double buffering
	glutSwapBuffers();
}

int _tmain(int argc, _TCHAR* argv[])
{
	// I've set the argv[1] to track.txt.
	// To change it, on the "Solution Explorer",
	// right click "assign1", choose "Properties",
	// go to "Configuration Properties", click "Debugging",
	// then type your track file name for the "Command Arguments"
	if (argc<2)
	{  
		printf ("usage: %s <trackfile>\n", argv[0]);
		exit(0);
	}
	loadSplines(argv[1]);

	glutInit(&argc,(char**)argv);

	/*create a window here double buffered and using depth testing */
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
	glutInitWindowSize(640,480);
	glutInitWindowPosition(50,50);
	glutCreateWindow(argv[0]);

	/* tells glut to use a particular display function to redraw */
	glutDisplayFunc(display);

	/* allow the user to quit using the right mouse button menu */
	g_iMenuId = glutCreateMenu(menufunc);
	glutSetMenu(g_iMenuId);
	glutAddMenuEntry("Quit",0);
	glutAddMenuEntry("Capture Animation",4);
	glutAttachMenu(GLUT_RIGHT_BUTTON);

	/* replace with any animate code */
	glutIdleFunc(doIdle);

	/* callback for mouse drags */
	glutMotionFunc(mousedrag);
	/* callback for idle mouse movement */
	glutPassiveMotionFunc(mouseidle);
	/* callback for mouse button changes */
	glutMouseFunc(mousebutton);
	/* callback for reshaping window, also sets viewport and perspective */
	glutReshapeFunc(reshape);

	//glutKeyboardFunc(keypress);

	/* do initialization */
	glInit();

	glutMainLoop();
	return 0;

}