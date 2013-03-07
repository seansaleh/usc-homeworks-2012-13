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

Pic * g_pGroundTexture;
GLuint groundTexture;

Pic * g_pSkyboxTextureTop;
GLuint skyboxTextureTop;
Pic * g_pSkyboxTextureLeft;
GLuint skyboxTextureLeft;
Pic * g_pSkyboxTextureFront;
GLuint skyboxTextureFront;
Pic * g_pSkyboxTextureRight;
GLuint skyboxTextureRight;
Pic * g_pSkyboxTextureBack;
GLuint skyboxTextureBack;

/* state of the world */
float g_vLandRotate[3] = {0.0, 0.0, 0.0};
float g_vLandTranslate[3] = {0.0, 0.0, 0.0};
float g_vLandScale[3] = {1.0, 1.0, 1.0};

double splineS = .5;

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


void matrix4mult(GLdouble *in1, GLdouble *in2, GLdouble *out) {
	for (int x = 0; x < 4; x++)
		for (int y = 0; y < 4; y++)
			out[4*x+y] = in1[y]*in2[4*x] + in1[4+y]*in2[1+4*x] + in1[8+y]*in2[2+4*x]+ in1[12+y]*in2[3+4*x];
}

point p(double u, spline *in, int x) {
	point outP;

	GLdouble uMatrix[16];
	uMatrix[0] = u*u*u;	uMatrix[4] = u*u;	uMatrix[8] = u;		uMatrix[12] = 1;	
	uMatrix[1] = 1;		uMatrix[5] = 1;		uMatrix[9] = 1;		uMatrix[13] = 1;	
	uMatrix[2] = 1;		uMatrix[6] = 1;		uMatrix[10] = 1;	uMatrix[14] = 1;	
	uMatrix[3] = 1;		uMatrix[7] = 1;		uMatrix[11] = 1;	uMatrix[15] = 1;	

	GLdouble basisMatrix[16];

	basisMatrix[0] = -1.0*splineS;	basisMatrix[4] = 2.0-splineS;	basisMatrix[8] = splineS-2.0;		basisMatrix[12] = splineS;
	basisMatrix[1] = 2.0*splineS;	basisMatrix[5] = splineS-3.0;	basisMatrix[9] = 3.0-2.0*splineS;	basisMatrix[13] = -1.0*splineS;
	basisMatrix[2] = -1.0*splineS;	basisMatrix[6] = 0.0;			basisMatrix[10] = splineS;			basisMatrix[14] = 0.0;
	basisMatrix[3] = 0.0;			basisMatrix[7] = 1.0;			basisMatrix[11] = 0.0;				basisMatrix[15] = 0.0;


	GLdouble controlM[16];

	controlM[0] = (*in).points[x].x;	controlM[4] = (*in).points[x].y;	controlM[8] =(*in).points[x].z;		controlM[12] = 1;
	controlM[1] = (*in).points[x+1].x;	controlM[5] = (*in).points[x+1].y;	controlM[9] =(*in).points[x+1].z;	controlM[13] = 1;
	controlM[2] = (*in).points[x+2].x;	controlM[6] = (*in).points[x+2].y;	controlM[10]=(*in).points[x+2].z;	controlM[14] = 1;
	controlM[3] = (*in).points[x+3].x;	controlM[7] = (*in).points[x+3].y;	controlM[11]=(*in).points[x+3].z;	controlM[15] = 1;

	GLdouble halfway[16];
	GLdouble output[16];
	matrix4mult(uMatrix, basisMatrix, halfway);
	matrix4mult(halfway, controlM, output);

	outP.x = output[0];
	outP.y = output[4];
	outP.z = output[8];

	return outP;
}


void renderSplines() {
	glBegin(GL_LINES);
	glLineWidth(5.0);
	glColor3f(1.0,0.0,0.0);

	point pPoint;
	point pPointprev;

	for ( int x = 0; x < g_iNumOfSplines; x++ )
		for ( int y = 0; y < g_Splines[x].numControlPoints-3; y++) {
			pPointprev = p((double)0.0,&(g_Splines[x]), y);
			for (int u = 0; u <= 100; u++) {
				pPoint = p((double)u/100.0,&(g_Splines[x]), y);
				glVertex3d(pPointprev.x,pPointprev.y,pPointprev.z);
				glVertex3d(pPoint.x,pPoint.y,pPoint.z);
				pPointprev = pPoint;
			}
		}
	glEnd();
}

void renderGround()
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, groundTexture);
	glBegin(GL_POLYGON);

	glColor3f(1.0, 1.0, 1.0);
	glTexCoord2f(0.0,0.0);	glVertex3f(-60.0, -60.0, -1.0);
	//glColor3f(0.0, 0.0, 1.0);
	glTexCoord2f(0.0,5.0);	glVertex3f(-60.0, 60.0, -1.0);
	//glColor3f(0.0, 0.0, 0.0);
	glTexCoord2f(5.0,5.0);	glVertex3f(60.0, 60.0, -1.0);
	//glColor3f(1.0, 1.0, 0.0);
	glTexCoord2f(5.0,0.0);	glVertex3f(60.0, -60.0, -1.0);

	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void renderSkybox()
{
	//glDisable(GL_DEPTH_TEST);
	//glPushMatrix();
	//glLoadIdentity();

	//To make the camera not move

	float d = 100.0f;
	glColor3f(1.0, 1.0, 1.0);

	glEnable(GL_TEXTURE_2D);

	/* Top */
	glBindTexture(GL_TEXTURE_2D, skyboxTextureTop);
	glBegin(GL_QUADS);
		glTexCoord2f(0,	0);		glVertex3f(	-d,	-d,	d);
		glTexCoord2f(1,	0);		glVertex3f(	 d,	-d,	d);
		glTexCoord2f(1,	1);		glVertex3f(	 d,	 d,	d);
		glTexCoord2f(0,	1);		glVertex3f(	-d,	 d,	d);
	glEnd();

	/* Left */
	glBindTexture(GL_TEXTURE_2D, skyboxTextureLeft);
	glBegin(GL_QUADS);
		glTexCoord2f(0,	0);		glVertex3f(	-d,	-d,	d);
		glTexCoord2f(1,	0);		glVertex3f(	-d,	d,	d);
		glTexCoord2f(1,	1);		glVertex3f(	-d,	d,	-d);
		glTexCoord2f(0,	1);		glVertex3f(	-d,	-d,	-d);
	glEnd();

	/* Front */
	glBindTexture(GL_TEXTURE_2D, skyboxTextureFront);
	glBegin(GL_QUADS);
		glTexCoord2f(0,	0);		glVertex3f(	-d,	d,	d);
		glTexCoord2f(1,	0);		glVertex3f(	d,	d,	d);
		glTexCoord2f(1,	1);		glVertex3f(	d,	d,	-d);
		glTexCoord2f(0,	1);		glVertex3f(	-d,	d,	-d);
	glEnd();

	/* Right */
	glBindTexture(GL_TEXTURE_2D, skyboxTextureRight);
	glBegin(GL_QUADS);
		glTexCoord2f(0,	0);		glVertex3f(	d,	d,	d);
		glTexCoord2f(1,	0);		glVertex3f(	d,	-d,	d);
		glTexCoord2f(1,	1);		glVertex3f(	d,	-d,	-d);
		glTexCoord2f(0,	1);		glVertex3f(	d,	d,	-d);
	glEnd();

	/* Back */
	glBindTexture(GL_TEXTURE_2D, skyboxTextureBack);
	glBegin(GL_QUADS);
		glTexCoord2f(0,	0);		glVertex3f(	d,	-d,	d);
		glTexCoord2f(1,	0);		glVertex3f(	-d,	-d,	d);
		glTexCoord2f(1,	1);		glVertex3f(	-d,	-d,	-d);
		glTexCoord2f(0,	1);		glVertex3f(	d,	-d,	-d);
	glEnd();


	glDisable(GL_TEXTURE_2D);
	//glEnable(GL_DEPTH_TEST);
	//glPopMatrix();
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

	/* Load top */
	g_pSkyboxTextureTop = jpeg_read("textures\\calm_top.jpg", NULL);
	if (!g_pSkyboxTextureTop)
	{
		printf ("error reading textures\\calm_top.jpg");
		exit(1);
	}
	glGenTextures(1, &skyboxTextureTop);
	glBindTexture(GL_TEXTURE_2D, skyboxTextureTop);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, 512, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pSkyboxTextureTop->pix);


	/* Load Left */
	g_pSkyboxTextureLeft = jpeg_read("textures\\calm_left.jpg", NULL);
	if (!g_pSkyboxTextureLeft)
	{
		printf ("error reading textures\\calm_left.jpg");
		exit(1);
	}
	glGenTextures(1, &skyboxTextureLeft);
	glBindTexture(GL_TEXTURE_2D, skyboxTextureLeft);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, 512, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pSkyboxTextureLeft->pix);


	/* Load Front */
	g_pSkyboxTextureFront = jpeg_read("textures\\calm_front.jpg", NULL);
	if (!g_pSkyboxTextureFront)
	{
		printf ("error reading textures\\calm_front.jpg");
		exit(1);
	}
	glGenTextures(1, &skyboxTextureFront);
	glBindTexture(GL_TEXTURE_2D, skyboxTextureFront);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, 512, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pSkyboxTextureFront->pix);

	/* Load Right */
	g_pSkyboxTextureRight = jpeg_read("textures\\calm_right.jpg", NULL);
	if (!g_pSkyboxTextureRight)
	{
		printf ("error reading textures\\calm_right.jpg");
		exit(1);
	}
	glGenTextures(1, &skyboxTextureRight);
	glBindTexture(GL_TEXTURE_2D, skyboxTextureRight);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, 512, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pSkyboxTextureRight->pix);

	/* Load Back */
	g_pSkyboxTextureBack = jpeg_read("textures\\calm_back.jpg", NULL);
	if (!g_pSkyboxTextureBack)
	{
		printf ("error reading textures\\calm_back.jpg");
		exit(1);
	}
	glGenTextures(1, &skyboxTextureBack);
	glBindTexture(GL_TEXTURE_2D, skyboxTextureBack);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); 
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, 512, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pSkyboxTextureBack->pix);

	/* Load ground texture */
	g_pGroundTexture = jpeg_read("textures\\ground.jpg", NULL);
	if (!g_pGroundTexture)
	{
		printf ("error reading textures\\ground.jpg");
		exit(1);
	}
	glGenTextures(1, &groundTexture);
	glBindTexture(GL_TEXTURE_2D, groundTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB, 512, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, g_pGroundTexture->pix);


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
	gluLookAt(-15,-15,10, 0,0,0,0,0,1);

	//Rotate
	glRotatef(g_vLandRotate[0],1.0,0.0,0.0);
	glRotatef(g_vLandRotate[1],0.0,1.0,0.0);
	glRotatef(g_vLandRotate[2],0.0,0.0,1.0);
	//Scale
	glScalef(g_vLandScale[0],g_vLandScale[1],g_vLandScale[2]);
	//Translate
	glTranslatef(g_vLandTranslate[0],g_vLandTranslate[1],g_vLandTranslate[2]);

	//Draw after this
	renderSkybox();
	renderGround();
	renderSplines();
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

	/* Matrix mult test code
	GLdouble in1[16] = {1,4,1,1,2,3,4,3,3,1,2,2,4,2,3,4};
	GLdouble in2[16] = {3,8,7,9,2,3,4,3,3,1,2,2,4,2,3,4};
	GLdouble out[16];
	matrix4mult(in1,in2,out);
	out[0];
	*/
	//{{1,2,3,4},{4,3,1,2},{1,4,2,3},{1,3,2,4}} * {{1,2,3,4},{4,3,1,2},{1,4,2,3},{1,3,2,4}}

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