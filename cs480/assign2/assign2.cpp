// assign2.cpp : Defines the entry point for the console application.
//

/*
	CSCI 480 Computer Graphics
	Assignment 2: Simulating a Roller Coaster
	C++ code by Sean Saleh
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

//Game State
int currentCoaster = 0;
int currentSpline = 0;
int currentU = 0;

//Texture pointers
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
float g_vLandTranslate[3] = {0.0, -0.5, 0.0};
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

/* Presupplied code */
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

/* Matrix Mulitply. Takes three 4x4 array, multiplies the first two and puts them in the out. Assumes out is initialized*/
void matrix4mult(GLdouble *in1, GLdouble *in2, GLdouble *out) { //Test
	for (int x = 0; x < 4; x++)
		for (int y = 0; y < 4; y++)
			out[4*x+y] = in1[y]*in2[4*x] + in1[4+y]*in2[1+4*x] + in1[8+y]*in2[2+4*x]+ in1[12+y]*in2[3+4*x];
}

/* Finds a point p given an array of splines, the specific spline, x, and the u along that spline*/
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

/*Returns a cross product from two 3x1 arrays of doubles. Assumes output is initialized*/
void crossproduct3d(GLdouble *in1, GLdouble *in2, GLdouble *output) {
	output[0] = in1[1]*in2[2] - in2[1] * in1[2];
	output[1] = in2[0]*in1[2] - in1[0] * in2[2];
	output[2] = in1[0]*in2[1] - in1[1] * in2[0];
}

/*Normalizes a 3x1 matrix of doubles*/
void normalize3d(GLdouble *input) {
	double magnitude = sqrt(input[0]*input[0] + input[1]*input[1] + input[2]*input[2]);
	if (magnitude == 0.0) //No divide by 0
		magnitude = 0.0001;

	input[0] /= magnitude;
	input[1] /= magnitude;
	input[2] /= magnitude;
}

/*Returns the tangent of a point along a spline. Outputs to a 3x1 array of doubles that is assumed to be initiliazed. Takes in pointer to array of splines, interger in that spline, and the u*/
void pTangent3d(double u, spline *in, int x, GLdouble *output) {
	point outP;

	GLdouble uMatrix[16];
	uMatrix[0] = 3*u*u;	uMatrix[4] = 2*u;	uMatrix[8] = 1;		uMatrix[12] = 0;	
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
	GLdouble tempoutput[16];
	matrix4mult(uMatrix, basisMatrix, halfway);
	matrix4mult(halfway, controlM, tempoutput);

	output[0] = tempoutput[0];
	output[1] = tempoutput[4];
	output[2] = tempoutput[8];

	normalize3d(output);
}

/*Returns a normalized tangent as a point from a given spline input*/
point pTangent(double u, spline *in, int x) {
	point outP;

	GLdouble uMatrix[16];
	uMatrix[0] = 3*u*u;	uMatrix[4] = 2*u;	uMatrix[8] = 1;		uMatrix[12] = 0;	
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

	GLdouble temp[] = {output[0], output[4], output[8]};
	normalize3d(temp);

	outP.x =temp[0];
	outP.y =temp[1];
	outP.z =temp[2];
	return outP;
}

/*Changes the global game state to move coaster forwards along the current spline*/
void forwards(){
	if (currentU < 100)
		currentU+=10;
	else if (currentSpline<g_Splines[currentCoaster].numControlPoints-4) {
		currentSpline++;
		currentU = 0;
	}
}

/*Changes the global game state to move coaster backwards along the current spline*/
void backwards() {
	if (currentU>0)
		currentU-=10;
	else if (currentSpline>0) {
		currentSpline--;
		currentU = 100;
	}
}

/* Moves the camera position by using glulookAt() to be at the current globally defined point along the coaster */
void rideCamera() {
	point pPoint = p((double)currentU/100.0,&(g_Splines[currentCoaster]), currentSpline);
	point tangentP = pTangent((double)currentU/100.0,&(g_Splines[currentCoaster]), currentSpline);
	gluLookAt(pPoint.x,pPoint.y,pPoint.z,pPoint.x+tangentP.x,pPoint.y+tangentP.y,pPoint.z+tangentP.z,0,0,1);

	/*DEBUG CODE
	GLdouble tangent[] = {0.,0.,0.};
	GLdouble arbitrary[] = {0.,0.,0.};
	GLdouble curN[] = {0.,0.,0.};
	GLdouble curB[] = {0.,0.,0.};

	pTangent3d((double)currentU/100.0,&(g_Splines[currentCoaster]), currentSpline, tangent);
	crossproduct3d(tangent, arbitrary, curN);
	normalize3d(curN);
	
	crossproduct3d(tangent, curN, curB);
	normalize3d(curB);

	glBegin(GL_LINES);
	glColor3f(1.0,0.0,0.0);
	glVertex3d(pPoint.x,pPoint.y,pPoint.z);
	glVertex3d(pPoint.x+curB[0],pPoint.y+ curB[1],pPoint.z+ curB[2]);

		glColor3f(0.0,1.0,0.0);
		glVertex3d(pPoint.x,pPoint.y,pPoint.z);
		glVertex3d(pPoint.x+curN[0],pPoint.y+ curN[1],pPoint.z+ curN[2]);
	glEnd();
	*/
}

/* Renders splines. Uses global variables loaded from loadSplines()*/
void renderSplines() {
	glBegin(GL_LINES);
	glLineWidth(5.0);
	glColor3f(1.0,0.0,0.0);

	point pPoint;
	point pPointprev;

	for ( int x = 0; x < g_iNumOfSplines; x++ )// For all tracks
		for ( int y = 0; y < g_Splines[x].numControlPoints-3; y++) {//For all splines in a track
			pPointprev = p((double)0.0,&(g_Splines[x]), y);
			for (int u = 0; u <= 100; u++) {//For all u's along the track
				pPoint = p((double)u/100.0,&(g_Splines[x]), y);
				glVertex3d(pPointprev.x,pPointprev.y,pPointprev.z);
				glVertex3d(pPoint.x,pPoint.y,pPoint.z);
				pPointprev = pPoint;
			}
		}
	glEnd();
}

/*Renders the rails as boxes. Draws quads defining the outside of a shape from the previous to current u value. Iterates through all splines*/
void renderRails() {
	glBegin(GL_QUADS);
	glColor3b(64,16,16);

	/*Modifier to make the rail cross-section smaller or larger*/
	double w = .05;
	double h = .05;

	/*Initialize all variables*/
	point temp;
	point tangentdebug;

	GLdouble tangent[] = {0.,0.,0.};
	GLdouble arbitrary[] = {1.,0.,1.};

	GLdouble prevPoint[] = {0.,0.,0.};
	GLdouble curPoint[] = {0.,0.,0.};

	GLdouble prevN[] = {0.,0.,0.};
	GLdouble curN[] = {0.,0.,0.};

	GLdouble prevB[] = {0.,0.,0.};
	GLdouble curB[] = {0.,0.,0.};

	for ( int x = 0; x < g_iNumOfSplines; x++ ) // For all tracks
		for ( int y = 0; y < g_Splines[x].numControlPoints-3; y++) {//For all splines in a track
			//Get the previous for the very first spline
			temp = p((double)0.0,&(g_Splines[x]), y);
			prevPoint[0] = temp.x; prevPoint[1] = temp.y; prevPoint[2] = temp.z;

			pTangent3d((double)0.0,&(g_Splines[x]), y, tangent);
			tangentdebug = pTangent((double)0.0,&(g_Splines[x]), y);

			crossproduct3d(tangent, arbitrary, prevN);
			normalize3d(prevN);

			crossproduct3d(tangent, prevN, prevB);
			normalize3d(prevB);

			for (int u = 0; u <= 100; u++) { //For all u's along the track
				/*Calculate Tangent, point, N, and B*/
				temp =  p((double)u/100.0,&(g_Splines[x]), y);
				curPoint[0] = temp.x; curPoint[1] = temp.y; curPoint[2] = temp.z;

				pTangent3d((double)u/100.0,&(g_Splines[x]), y, tangent);
				crossproduct3d(tangent, arbitrary, curN);
				normalize3d(curN);

				crossproduct3d(tangent, curN, curB);
				normalize3d(curB);

				/*Define all of the corners of the "box"*/
				GLdouble v0[] = {prevPoint[0] + prevN[0]*w - prevB[0]*h	,prevPoint[1] +  prevN[1]*w - prevB[1]*h	,prevPoint[2] +  prevN[2]*w - prevB[2]*h	};
				GLdouble v1[] = {prevPoint[0] + prevN[0]*w + prevB[0]*h	,prevPoint[1] +  prevN[1]*w + prevB[1]*h	,prevPoint[2] +  prevN[2]*w + prevB[2]*h	};
				GLdouble v2[] = {prevPoint[0] - prevN[0]*w + prevB[0]*h	,prevPoint[1] -  prevN[1]*w + prevB[1]*h	,prevPoint[2] -  prevN[2]*w + prevB[2]*h	};
				GLdouble v3[] = {prevPoint[0] - prevN[0]*w - prevB[0]*h	,prevPoint[1] -  prevN[1]*w - prevB[1]*h	,prevPoint[2] -  prevN[2]*w - prevB[2]*h	};
				GLdouble v4[] = {curPoint[0]  + curN[0]*w  - curB[0]*h	,curPoint[1]  +  curN[1]*w - curB[1]*h		,curPoint[2]  +  curN[2]*w  - curB[2]*h		};
				GLdouble v5[] = {curPoint[0]  + curN[0]*w  + curB[0]*h	,curPoint[1]  +  curN[1]*w + curB[1]*h		,curPoint[2]  +  curN[2]*w  + curB[2]*h		};
				GLdouble v6[] = {curPoint[0]  - curN[0]*w  + curB[0]*h	,curPoint[1]  -  curN[1]*w + curB[1]*h		,curPoint[2]  -  curN[2]*w  + curB[2]*h		};
				GLdouble v7[] = {curPoint[0]  - curN[0]*w  - curB[0]*h	,curPoint[1]  -  curN[1]*w - curB[1]*h		,curPoint[2]  -  curN[2]*w  - curB[2]*h		};

				/*Draw each poly*/
				glVertex3dv(v0); glVertex3dv(v1); glVertex3dv(v5); glVertex3dv(v4);

				glVertex3dv(v1); glVertex3dv(v2); glVertex3dv(v6); glVertex3dv(v5);

				glVertex3dv(v2); glVertex3dv(v3); glVertex3dv(v7); glVertex3dv(v6);

				glVertex3dv(v0); glVertex3dv(v3); glVertex3dv(v7); glVertex3dv(v4);

				/*Set current to previous*/
				prevPoint[0] = curPoint[0];	prevPoint[1] = curPoint[1];	prevPoint[2] = curPoint[2]; 
				prevN[0] = curN[0];			prevN[1] = curN[1];			prevN[2] = curN[2]; 
				prevB[0] = curB[0];			prevB[1] = curB[1];			prevB[2] = curB[2]; 
			}
		}
	glEnd();
}

/*Texture maps the ground and draws a polygon for the ground*/
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

/*Renders 5 quads in the distance as a skybox. Uses a seperate texture for each quad. Texture is designed to look seamless when used as a skybox*/
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

void keypress(unsigned char key, int x, int y){
	switch (key)
	{
	case 'q':
		exit(0);
		break;
	case 'w':
		forwards();
		break;
	case 's':
		backwards();
		break;
	}
}

/*Initializes textures and loads them in*/
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
	gluPerspective(60.0,1.0*width/height, 0.01, 500.0);

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
		
		//animate
		forwards();

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
	//gluLookAt(-15,-15,10, 0,0,0,0,0,1);

	//Rotate
	glRotatef(g_vLandRotate[0],1.0,0.0,0.0);
	glRotatef(g_vLandRotate[1],0.0,1.0,0.0);
	glRotatef(g_vLandRotate[2],0.0,0.0,1.0);
	//Scale
	glScalef(g_vLandScale[0],g_vLandScale[1],g_vLandScale[2]);
	//Translate
	glTranslatef(g_vLandTranslate[0],g_vLandTranslate[1],g_vLandTranslate[2]);
	
	rideCamera();

	//Draw after this
	renderSkybox();
	renderGround();
	//renderSplines();
	renderRails();

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

	glutKeyboardFunc(keypress);

	/* do initialization */
	glInit();

	glutMainLoop();
	return 0;

}