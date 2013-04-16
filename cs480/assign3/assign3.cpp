/*
CSCI 480
Assignment 3 Raytracer

Name: Sean Saleh
*/

#include <pic.h>
#include <windows.h>
#include <stdlib.h>
#include <GL/glu.h>
#include <GL/glut.h>

#include <stdio.h>
#include <string>

#include <math.h>
#pragma region one
#define MAX_TRIANGLES 2000
#define MAX_SPHERES 10
#define MAX_LIGHTS 10

char *filename=0;

//different display modes
#define MODE_DISPLAY 1
#define MODE_JPEG 2
int mode=MODE_DISPLAY;

//you may want to make these smaller for debugging purposes
#define WIDTH 640
#define HEIGHT 480

//the field of view of the camera
#define fov 60.0
#define M_PI       3.14159265358979323846

unsigned char buffer[HEIGHT][WIDTH][3];
#pragma endregion
struct Vertex
{
  double position[3];
  double color_diffuse[3];
  double color_specular[3];
  double normal[3];
  double shininess;
};

typedef struct _Triangle
{
  struct Vertex v[3];
} Triangle;
#pragma region Region_Two
typedef struct _Sphere
{
  double position[3];
  double color_diffuse[3];
  double color_specular[3];
  double shininess;
  double radius;
} Sphere;

typedef struct _Light
{
  double position[3];
  double color[3];
} Light;

Triangle triangles[MAX_TRIANGLES];
Sphere spheres[MAX_SPHERES];
Light lights[MAX_LIGHTS];
double ambient_light[3];

int num_triangles=0;
int num_spheres=0;
int num_lights=0;

int debug = 0;
int stop = 229;

double perpixel_width;
double perpixel_height;
double screen_left;
double screen_bottom;

void plot_pixel_display(int x,int y,unsigned char r,unsigned char g,unsigned char b);
void plot_pixel_jpeg(int x,int y,unsigned char r,unsigned char g,unsigned char b);
void plot_pixel(int x,int y,unsigned char r,unsigned char g,unsigned char b);
#pragma endregion
unsigned char clamp_convert(double color) { /*Expects a float for color hopefully between 0 and 1 */
	if (color > 1.f)
		color = 1.f;
	else if (color <= 0.f)
	{
		color = 0.f;
		return 0; //To prevent devide by 0 errors
	}
	return (unsigned char) (255 * color);
}

void set_global_perpixel_distance() {
	double aspect;
	aspect = (double)WIDTH / (double)HEIGHT;
	double left = - aspect * tan(fov/2.f*M_PI/180);
	double right = aspect * tan(fov/2.f*M_PI/180);
	double top = tan((fov/2.f)*(M_PI/180));
	double bottom = - tan(fov/2.f*M_PI/180);
	// actually solve for these corners

	double width = right-left;
	double height = top-bottom;

	perpixel_width = width / WIDTH;
	perpixel_height = height / HEIGHT;
	screen_left = left;
	screen_bottom = bottom;
}

void convert_world_position(double *screen_position, double x, double y) {
	screen_position[2] = -1.f;
	screen_position[0] = screen_left + perpixel_width/2 + x*perpixel_width;
	screen_position[1] = screen_bottom + perpixel_height/2 + y*perpixel_height;
}

void normalize3d(double *input, double *output) {
	double magnitude = sqrt(input[0]*input[0] + input[1]*input[1] + input[2]*input[2]);
	if (magnitude == 0.0) //No divide by 0
		magnitude = 0.0001;

	output[0] = input[0] / magnitude;
	output[1] = input[1] / magnitude;
	output[2] = input[2] / magnitude;
}

double dot_product(double *a, double * b) {
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void vector3_minus (double *a, double *b, double *out) {
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

/*Returns a cross product from two 3x1 arrays of doubles. Assumes output is initialized*/
void vector3_cross(double *in1, double *in2, double *output) {
	output[0] = in1[1]*in2[2] - in2[1] * in1[2];
	output[1] = in2[0]*in1[2] - in1[0] * in2[2];
	output[2] = in1[0]*in2[1] - in1[1] * in2[0];
}

/*Assume that return_color has some value */
void sphere_phong_color(double * hit_location, double* light_position, double * sphere_position, double * return_color, double * light_color, double * color_diffuse, double *color_specular, double shininess) {
	double color = 0.0f;

	double normal[3];
	normal[0] = hit_location[0] - sphere_position[0];
	normal[1] = hit_location[1] - sphere_position[1];
	normal[2] = hit_location[2] - sphere_position[2];
	normalize3d(normal, normal);

	double view_vector[3];
	view_vector[0] = -hit_location[0];	
	view_vector[1] = -hit_location[1];	
	view_vector[2] = -hit_location[2];	
	normalize3d(view_vector, view_vector);

	double light_vector[3];
	light_vector[0] = light_position[0] - hit_location[0];
	light_vector[1] = light_position[1] - hit_location[1];
	light_vector[2] = light_position[2] - hit_location[2];
	normalize3d(light_vector, light_vector);

	double l_dot_n = dot_product(light_vector, normal);
	double reflected_vector[3]; // r = 2 * l_dot_n * n - l
	reflected_vector[0] =2 * l_dot_n * normal[0] - light_vector[0];
	reflected_vector[1] =2 * l_dot_n * normal[1] - light_vector[1];
	reflected_vector[2] =2 * l_dot_n * normal[2] - light_vector[2];
	normalize3d(reflected_vector, reflected_vector);

	double r_dot_v = dot_product(reflected_vector, view_vector);
	if (l_dot_n < 0.f)
		l_dot_n = 0.f;
	if (r_dot_v < 0.f)
		r_dot_v = 0.f;

	return_color[0] += light_color[0] * (color_diffuse[0] * (l_dot_n) + color_specular[0] * pow(r_dot_v,shininess));
	return_color[1] += light_color[1] * (color_diffuse[1] * (l_dot_n) + color_specular[1] * pow(r_dot_v,shininess));
	return_color[2] += light_color[2] * (color_diffuse[2] * (l_dot_n) + color_specular[2] * pow(r_dot_v,shininess));
}

void triangle_phong_color(double * hit_location, double* light_position, Triangle * triangle, double * return_color, double * light_color) {
	double color = 0.0f;

	double color_diffuse[3];
	double color_specular[3];
	double shininess;
	
	double normal[3];

	double percent_p0 = 1.f/3.f;
	double percent_p1 = 1.f/3.f;
	double percent_p2 = 1.f/3.f;

	/*
	 * None of this will work... think about a right triangle with the point being the cetner of the hypotenous
	 *
	 * CORRECT math is to do the area% opposite the vertex is how much of that vertex is used
	 *
	//Normal is (I think... this makes sense to me!)
	//magnitude = |hit-p0| + |hit-p1| + |hit-p2|
	//normal is normalized( |hit-p0|/magnitude * p0.normal + |hit-p1|/magnitude * p1.normal + |hit-p2|/magnitude * p2.normal )
	//normal is normalized( magnitude - |hit-p0| * p0.normal
	double hit_p1[3];	 vector3_minus(hit_location, triangle->v[0].position, hit_p1);
	double hit_p2[3];	 vector3_minus(hit_location, triangle->v[1].position, hit_p2);
	double hit_p3[3];	 vector3_minus(hit_location, triangle->v[2].position, hit_p3);

	double mag_hit_p1 = sqrt(hit_p1[0]*hit_p1[0] + hit_p1[1]*hit_p1[1] + hit_p1[2]*hit_p1[2]);
	double mag_hit_p2 = sqrt(hit_p2[0]*hit_p2[0] + hit_p2[1]*hit_p2[1] + hit_p2[2]*hit_p2[2]);
	double mag_hit_p3 = sqrt(hit_p3[0]*hit_p3[0] + hit_p3[1]*hit_p3[1] + hit_p3[2]*hit_p3[2]);
	double magnitude = sqrt(mag_hit_p1*mag_hit_p1 + mag_hit_p2*mag_hit_p2 + mag_hit_p3*mag_hit_p3);
	*/
	normal[0] = percent_p0 * triangle->v[0].normal[0] + percent_p1 * triangle->v[1].normal[0] + percent_p2 * triangle->v[2].normal[0];
	normal[1] = percent_p0 * triangle->v[0].normal[1] + percent_p1 * triangle->v[1].normal[1] + percent_p2 * triangle->v[2].normal[1];
	normal[2] = percent_p0 * triangle->v[0].normal[2] + percent_p1 * triangle->v[1].normal[2] + percent_p2 * triangle->v[2].normal[2];

	normalize3d(normal, normal);

	//Now to adjust colors based on the same distances...
	//The same math should work
	color_diffuse[0] = percent_p0 * triangle->v[0].color_diffuse[0] + percent_p1 * triangle->v[1].color_diffuse[0] + percent_p2 * triangle->v[2].color_diffuse[0];
	color_diffuse[1] = percent_p0 * triangle->v[0].color_diffuse[1] + percent_p1 * triangle->v[1].color_diffuse[1] + percent_p2 * triangle->v[2].color_diffuse[1];
	color_diffuse[2] = percent_p0 * triangle->v[0].color_diffuse[2] + percent_p1 * triangle->v[1].color_diffuse[2] + percent_p2 * triangle->v[2].color_diffuse[2];

	color_specular[0] = percent_p0 * triangle->v[0].color_specular[0] + percent_p1 * triangle->v[1].color_specular[0] +percent_p2 * triangle->v[2].color_specular[0];
	color_specular[1] = percent_p0 * triangle->v[0].color_specular[1] + percent_p1 * triangle->v[1].color_specular[1] +percent_p2 * triangle->v[2].color_specular[1];
	color_specular[2] = percent_p0 * triangle->v[0].color_specular[2] + percent_p1 * triangle->v[1].color_specular[2] +percent_p2 * triangle->v[2].color_specular[2];

	shininess = percent_p0 * triangle->v[0].shininess + percent_p1 * triangle->v[1].shininess + percent_p2 * triangle->v[2].shininess;

	double view_vector[3];
	view_vector[0] = -hit_location[0];	
	view_vector[1] = -hit_location[1];	
	view_vector[2] = -hit_location[2];	
	normalize3d(view_vector, view_vector);

	double light_vector[3];
	light_vector[0] = light_position[0] - hit_location[0];
	light_vector[1] = light_position[1] - hit_location[1];
	light_vector[2] = light_position[2] - hit_location[2];
	normalize3d(light_vector, light_vector);
	
	double l_dot_n = dot_product(light_vector, normal);
	double reflected_vector[3]; // r = 2 * l_dot_n * n - l
	reflected_vector[0] =2 * l_dot_n * normal[0] - light_vector[0];
	reflected_vector[1] =2 * l_dot_n * normal[1] - light_vector[1];
	reflected_vector[2] =2 * l_dot_n * normal[2] - light_vector[2];
	normalize3d(reflected_vector, reflected_vector);
	
	double r_dot_v = dot_product(reflected_vector, view_vector);
	if (l_dot_n < 0.f)
		l_dot_n = 0.f;
	if (r_dot_v < 0.f)
		r_dot_v = 0.f;
	
	return_color[0] += light_color[0] * (color_diffuse[0] * (l_dot_n) + color_specular[0] * pow(r_dot_v,shininess));
	return_color[1] += light_color[1] * (color_diffuse[1] * (l_dot_n) + color_specular[1] * pow(r_dot_v,shininess));
	return_color[2] += light_color[2] * (color_diffuse[2] * (l_dot_n) + color_specular[2] * pow(r_dot_v,shininess));
	
}

/*TODO*/
Triangle * collide_triangle(double *direction, double * distance_out, double * translation) {
	Triangle * cur_triangle = NULL;
	double normal_ray[3];

	double transformed_direction[3];
	transformed_direction[0] = direction[0] - translation[0];
	transformed_direction[1] = direction[1] - translation[1];
	transformed_direction[2] = direction[2] - translation[2];
	normalize3d(transformed_direction, transformed_direction);

	for(int x = 0; x < num_triangles; x++) {
		//Get intersection point with polygon
		//t = -(o-p)_dot_n/n_dot_d
		//n_dot_d == 0
		double n[3];
		double p1_p0[3];	 vector3_minus(triangles[x].v[1].position,triangles[x].v[0].position,p1_p0);
		double p2_p0[3];	 vector3_minus(triangles[x].v[2].position,triangles[x].v[0].position,p2_p0);
		vector3_cross(p1_p0, p2_p0, n);
		normalize3d(n, n);
		double n_dot_d = dot_product(n,transformed_direction);
		if (!(n_dot_d < 0.000001f && n_dot_d > 0.000001f)) { //If n_dot_p is zero, then this triangle is parallel to ray
			double origin_minus_p[3]; //Check here if erroring, it could be the other way around?
			origin_minus_p[0] = translation[0] - triangles[x].v[0].position[0];
			origin_minus_p[1] = translation[1] - triangles[x].v[0].position[1];
			origin_minus_p[2] = translation[2] - triangles[x].v[0].position[2];
			double o_minus_p_dot_n = dot_product(origin_minus_p, n);
			double t = - o_minus_p_dot_n/n_dot_d;
			if (t>-.0000001f && t < .0000001f)
				t = 0.f;
			if (t>0.f) { //If the hit location is in front of us
				double hit[3];
				hit[0] = translation[0] + t * transformed_direction[0];
				hit[1] = translation[1] + t * transformed_direction[1];
				hit[2] = translation[2] + t * transformed_direction[2];
				/* From math for checking if triangles are to the left of the lines so it is on the plane
				(p1-p0)cross(hit-p0)dot n>=0
				(p2-p1)cross(hit-p1)dot n>=0
				(p0-p2)cross(hit-p2)dot n>=0
				*/
				/*This is already defined above *///double p1_p0[3];	 vector3_minus(triangles[x].v[1].position,triangles[x].v[0].position,p1_p0);
				double p2_p1[3];	 vector3_minus(triangles[x].v[2].position,triangles[x].v[1].position,p2_p1);
				double p0_p2[3];	 vector3_minus(triangles[x].v[0].position,triangles[x].v[2].position,p0_p2);
				double hit_p1[3];	 vector3_minus(hit, triangles[x].v[0].position, hit_p1);
				double hit_p2[3];	 vector3_minus(hit, triangles[x].v[1].position, hit_p2);
				double hit_p3[3];	 vector3_minus(hit, triangles[x].v[2].position, hit_p3);
				 
				double cross_1[3];	 vector3_cross(p1_p0,hit_p1,cross_1);
				double cross_2[3];	 vector3_cross(p2_p1,hit_p2,cross_2);
				double cross_3[3];	 vector3_cross(p0_p2,hit_p3,cross_3);

				if (dot_product(cross_1, n) >=0.0f && dot_product(cross_2, n) >=0.0f && dot_product(cross_3, n) >=0.0f) {
					if (t<*distance_out) {
						*distance_out = t;
						cur_triangle = &triangles[x];
					}
				}
			}
		}
	}
	return cur_triangle;
}

Sphere* collide_sphere(double * direction, double * distance_out, double * translation){
	Sphere * cur_sphere = NULL;
	double normal_ray[3];

	double transformed_direction[3];
	transformed_direction[0] = direction[0] - translation[0];
	transformed_direction[1] = direction[1] - translation[1];
	transformed_direction[2] = direction[2] - translation[2];
	normalize3d(transformed_direction, normal_ray);

	for(int x = 0; x < num_spheres; x++) {
		/*This math from the slides for ray-sphere intersection*/
		double b = 2 * (normal_ray[0]*(translation[0]-spheres[x].position[0]) + normal_ray[1]*(translation[1]-spheres[x].position[1]) + normal_ray[2]*(translation[2]-spheres[x].position[2]));
		double c = (translation[0]-spheres[x].position[0])*(translation[0]-spheres[x].position[0]) + (translation[1]-spheres[x].position[1])*(translation[1]-spheres[x].position[1]) + (translation[2]-spheres[x].position[2])*(translation[2]-spheres[x].position[2]) - spheres[x].radius*spheres[x].radius;
		double inside = b*b - 4 * c;

		if (inside >=0) { //Else unreal answer, abort
			double t0 = (-b + sqrt(inside))/2;
			double t1 = (-b - sqrt(inside))/2;
			if (t0>-0.0001f && t0 <= 0.0001f)
				t0 = 0.0f;
			if (t1>-0.0001f && t1 < 0.0001f)
				t1 = 0.0f; 
			/*Note, if the ray is cast from within the sphere, it will hit that sphere*/
			if (t0 > 0.f && t0 < *distance_out) { //If Closer
				*distance_out = t0;
				cur_sphere = &spheres[x];
			}
			if (t1 > 0.f && t1 < *distance_out) { //If Closerer
				*distance_out = t1;
				cur_sphere = &spheres[x];
			}
		}
	}
	return cur_sphere;
}

bool check_in_shadow(double * source_transform, Light * destination_light) {
	/*To make sure that it doesn't collide with anything past the light*/
	double light_sphere_distance = sqrt((destination_light->position[0]-source_transform[0])*(destination_light->position[0]-source_transform[0]) + (destination_light->position[1]-source_transform[1])*(destination_light->position[1]-source_transform[1]) + (destination_light->position[2]-source_transform[2])*(destination_light->position[2]-source_transform[2]));
	double light_tri_distance = light_sphere_distance;

	if (collide_sphere(destination_light->position, &light_sphere_distance, source_transform))
		return true;
	if(collide_triangle(destination_light->position, &light_tri_distance, source_transform))
		return true;
	return false;
}

void cast_ray(double x, double y, double *color) {
	color[0] = ambient_light[0];
	color[1] = ambient_light[1];
	color[2] = ambient_light[2];

	double screen_position[3];
	convert_world_position(screen_position, x, y);

	double translation [3] = {0.0f, 0.0f, 0.0f};

	double sphere_distance	= 200000000000.f;
	Sphere *hit_sphere = collide_sphere(screen_position, &sphere_distance, translation);
	double tri_distance		= 100000000000.f;
	Triangle *hit_triangle = collide_triangle(screen_position, &tri_distance, translation);

	double ray_hit_location[3];
	double normal_ray[3];
	normalize3d(screen_position, normal_ray);
	if (sphere_distance<tri_distance && hit_sphere) {
		//Convert hit_sphere->position to actual hit location using distance and camera normal
		ray_hit_location[0] = sphere_distance * normal_ray[0];	
		ray_hit_location[1] = sphere_distance * normal_ray[1];	
		ray_hit_location[2] = sphere_distance * normal_ray[2];
		for (int x = 0; x < num_lights; x++ ) {
			if (!check_in_shadow(ray_hit_location, &lights[x])) {//If not in shadow
				sphere_phong_color(ray_hit_location, lights[x].position, hit_sphere->position, color, lights[x].color, hit_sphere->color_diffuse, hit_sphere->color_specular, hit_sphere->shininess);
			}
		}
	} else if (hit_triangle) {
		ray_hit_location[0] = tri_distance * normal_ray[0];	
		ray_hit_location[1] = tri_distance * normal_ray[1];	
		ray_hit_location[2] = tri_distance * normal_ray[2];
		for (int x = 0; x < num_lights; x++ ) {
			if (!check_in_shadow(ray_hit_location, &lights[x])) {//If not in shadow
				triangle_phong_color(ray_hit_location, lights[x].position, hit_triangle, color, lights[x].color);
			}
		}
	} else {//else didn't hit 
		color[0] = 1.0f; color[1] = 1.0f; color[2] = 1.0f;
	}
}
#pragma region prewritten
void cast_aa_ray(int x, int y) {

	/*Yay hand unrolled loops!*/
	double color1[3];
	double color2[3];
	double color3[3];
	double color4[3];
	cast_ray(x+.25f, y+.5f, color1);
	cast_ray(x+.5f, y+.75f, color2);
	cast_ray(x+.5f, y+.25f, color3);
	cast_ray(x+.75f, y+.5f, color4);

	double color[3];
	color[0] = (color1[0]+color2[0]+color3[0]+color4[0]) / 4;
	color[1] = (color1[1]+color2[1]+color3[1]+color4[1]) / 4;
	color[2] = (color1[2]+color2[2]+color3[2]+color4[2]) / 4;

	plot_pixel(x,y,clamp_convert(color[0]),clamp_convert(color[1]),clamp_convert(color[2]));
}
void draw_scene()
{
  set_global_perpixel_distance();
  unsigned int x,y;
  //simple output
  for(x=0; x<WIDTH; x++)
  {
    glPointSize(2.0);  
    glBegin(GL_POINTS);
    for(y=0;y < HEIGHT;y++)
    {
		cast_aa_ray(x,y);
    }
    glEnd();
    glFlush();
  }
  printf("Done!\n"); fflush(stdout);
}
void plot_pixel_display(int x,int y,unsigned char r,unsigned char g,unsigned char b)
{
  glColor3f(((double)r)/256.f,((double)g)/256.f,((double)b)/256.f);
  glVertex2i(x,y);
}
void plot_pixel_jpeg(int x,int y,unsigned char r,unsigned char g,unsigned char b)
{
  buffer[HEIGHT-y-1][x][0]=r;
  buffer[HEIGHT-y-1][x][1]=g;
  buffer[HEIGHT-y-1][x][2]=b;
}
void plot_pixel(int x,int y,unsigned char r,unsigned char g, unsigned char b)
{
  plot_pixel_display(x,y,r,g,b);
  if(mode == MODE_JPEG)
      plot_pixel_jpeg(x,y,r,g,b);
}
void save_jpg()
{
  Pic *in = NULL;

  in = pic_alloc(640, 480, 3, NULL);
  printf("Saving JPEG file: %s\n", filename);

  memcpy(in->pix,buffer,3*WIDTH*HEIGHT);
  if (jpeg_write(filename, in))
    printf("File saved Successfully\n");
  else
    printf("Error in Saving\n");

  pic_free(in);      

}
void parse_check(char *expected,char *found)
{
  if(stricmp(expected,found))
    {
      char error[100];
      printf("Expected '%s ' found '%s '\n",expected,found);
      printf("Parse error, abnormal abortion\n");
      exit(0);
    }

}
void parse_doubles(FILE*file, char *check, double p[3])
{
  char str[100];
  fscanf(file,"%s",str);
  parse_check(check,str);
  fscanf(file,"%lf %lf %lf",&p[0],&p[1],&p[2]);
  printf("%s %lf %lf %lf\n",check,p[0],p[1],p[2]);
}
void parse_rad(FILE*file,double *r)
{
  char str[100];
  fscanf(file,"%s",str);
  parse_check("rad:",str);
  fscanf(file,"%lf",r);
  printf("rad: %f\n",*r);
}
void parse_shi(FILE*file,double *shi)
{
  char s[100];
  fscanf(file,"%s",s);
  parse_check("shi:",s);
  fscanf(file,"%lf",shi);
  printf("shi: %f\n",*shi);
}
int loadScene(char *argv)
{
  FILE *file = fopen(argv,"r");
  int number_of_objects;
  char type[50];
  int i;
  Triangle t;
  Sphere s;
  Light l;
  fscanf(file,"%i",&number_of_objects);

  printf("number of objects: %i\n",number_of_objects);
  char str[200];

  parse_doubles(file,"amb:",ambient_light);

  for(i=0;i < number_of_objects;i++)
    {
      fscanf(file,"%s\n",type);
      printf("%s\n",type);
      if(stricmp(type,"triangle")==0)
	{

	  printf("found triangle\n");
	  int j;

	  for(j=0;j < 3;j++)
	    {
	      parse_doubles(file,"pos:",t.v[j].position);
	      parse_doubles(file,"nor:",t.v[j].normal);
	      parse_doubles(file,"dif:",t.v[j].color_diffuse);
	      parse_doubles(file,"spe:",t.v[j].color_specular);
	      parse_shi(file,&t.v[j].shininess);
	    }

	  if(num_triangles == MAX_TRIANGLES)
	    {
	      printf("too many triangles, you should increase MAX_TRIANGLES!\n");
	      exit(0);
	    }
	  triangles[num_triangles++] = t;
	}
      else if(stricmp(type,"sphere")==0)
	{
	  printf("found sphere\n");

	  parse_doubles(file,"pos:",s.position);
	  parse_rad(file,&s.radius);
	  parse_doubles(file,"dif:",s.color_diffuse);
	  parse_doubles(file,"spe:",s.color_specular);
	  parse_shi(file,&s.shininess);

	  if(num_spheres == MAX_SPHERES)
	    {
	      printf("too many spheres, you should increase MAX_SPHERES!\n");
	      exit(0);
	    }
	  spheres[num_spheres++] = s;
	}
      else if(stricmp(type,"light")==0)
	{
	  printf("found light\n");
	  parse_doubles(file,"pos:",l.position);
	  parse_doubles(file,"col:",l.color);

	  if(num_lights == MAX_LIGHTS)
	    {
	      printf("too many lights, you should increase MAX_LIGHTS!\n");
	      exit(0);
	    }
	  lights[num_lights++] = l;
	}
      else
	{
	  printf("unknown type in scene description:\n%s\n",type);
	  exit(0);
	}
    }
  return 0;
}
void display()
{

}
void init()
{
  glMatrixMode(GL_PROJECTION);
  glOrtho(0,WIDTH,0,HEIGHT,1,-1);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glClearColor(0,0,0,0);
  glClear(GL_COLOR_BUFFER_BIT);
}
void idle()
{
  //hack to make it only draw once
  static int once=0;
  if(!once)
  {
      draw_scene();
      if(mode == MODE_JPEG)
		save_jpg();
    }
  once=1;
}
int main (int argc, char ** argv)
{
  if (argc<2 || argc > 3)
  {  
    printf ("usage: %s <scenefile> [jpegname]\n", argv[0]);
    exit(0);
  }
  if(argc == 3)
    {
      mode = MODE_JPEG;
      filename = argv[2];
    }
  else if(argc == 2)
    mode = MODE_DISPLAY;

  glutInit(&argc,argv);
  loadScene(argv[1]);

  glutInitDisplayMode(GLUT_RGBA | GLUT_SINGLE);
  glutInitWindowPosition(0,0);
  glutInitWindowSize(WIDTH,HEIGHT);
  int window = glutCreateWindow("Ray Tracer");
  glutDisplayFunc(display);
  glutIdleFunc(idle);
  init();
  glutMainLoop();
}
#pragma endregion
