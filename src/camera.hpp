#ifndef CAMERA_H
#define CAMERA_H

#include <vector>
#include <thread>
#include <future>
#include <mutex>
#include <omp.h>

#include "rtweekend.hpp"

#include "hittable.hpp"
#include "material.hpp"


//REMEMBER THE CAMERA HAS RIGHT HANDED COORDINATES

class camera {
  public:
    double aspect_ratio =       1.0;    // Ratio of image width over height
    int    IMG_W                = 100;  // Rendered image width in pixel count
    int    samples_per_pixel    = 10;   // Number of random samples per pixel
    int    max_depth            = 10;   // MAx no fo ray bounces into scene (including inside sphere)

    double vfov = 90;

    point3 lookFrom = point3(0,0,0);
    point3 lookAt = point3(0,0,-1);
    vec3 vup = vec3(0,1,0);             //camera relative up dir

    double defocus_angle = 0;           //variation agle of rays through each pixel
    double focus_dist = 10;             //dist from camera lookFrom point to plane of percect focus


    #pragma omp declare reduction(+: vec3: omp_out += omp_in) initializer(omp_priv = vec3())

    void render(const hittable& world) 
    {
        initialize();

        std::cout << "P3\n" << IMG_W << ' ' << IMG_H << "\n255\n";

        omp_set_num_threads(16);

        for (int j = 0; j < IMG_H; j++) 
        {
            std::clog << "\rScanlines remaining: " << (IMG_H - j) << ' ' << std::flush;
            for (int i = 0; i < IMG_W; i++) 
            {
                color pixel_color(0, 0, 0);
                #pragma omp parallel for reduction(+:pixel_color)

                for(int sample = 0; sample < samples_per_pixel; sample++) 
                {
                    ray r = get_ray(i, j);
                    pixel_color += ray_color(r,max_depth, world);
                }
                color_render(std::cout,pixel_samples_scale * pixel_color);
            }
        }

        std::clog << "\rDone.                 \n";
    }
  private:
    /* Private Camera Variables Here */
    int    IMG_H;   // Rendered image height
    point3 center;         // Camera center
    point3 firstpixel_loc;    // Location of pixel 0, 0
    vec3   pixel_delta_u;  // Offset to pixel to the right
    vec3   pixel_delta_v;  // Offset to pixel below

    vec3 u,v,w;

    vec3   defocus_disk_u;       // Defocus disk horizontal radius
    vec3   defocus_disk_v;       // Defocus disk vertical radius

    double pixel_samples_scale; //Color scale factor for a sum of pixel samples

    void initialize() {
        IMG_H = int(IMG_W / aspect_ratio);
        IMG_H = (IMG_H < 1) ? 1 : IMG_H;

        pixel_samples_scale = 1.0 / samples_per_pixel;

        center = lookFrom;

        // Determine viewport dimensions.
        // auto focal_length = (lookFrom-lookAt).length();      //dont need it cuz focus_dist
        
        auto theta = degrees_to_radians(vfov);
        auto h = std::tan(theta/2);

        auto viewport_height = 2 * h * focus_dist;
        auto viewport_width = viewport_height * (double(IMG_W)/IMG_H);

        w = unit_vector(lookFrom - lookAt);
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        // Calculate the vectors across the horizontal and down the vertical viewport edges.
        auto viewport_u = viewport_width * u;
        auto viewport_v = viewport_height * -v;

        // Calculate the horizontal and vertical delta vectors from pixel to pixel.
        pixel_delta_u = viewport_u / IMG_W;
        pixel_delta_v = viewport_v / IMG_H;

        // Calculate the location of the upper left pixel.
        auto viewport_upper_left = center - (focus_dist * w) - viewport_u/2 - viewport_v/2;
        firstpixel_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        // Calculate the camera defocus disk basis vectors.
        auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }

    ray get_ray(int i, int j) const 
    {
        // Construct a camera ray originating from the defocus disk and directed at a randomly
        // sampled point around the pixel location i, j.


        auto offset = sample_square();
        auto pixel_sample = firstpixel_loc 
                          + ((i + offset.x()) * pixel_delta_u)
                          + ((j + offset.y()) * pixel_delta_v);

        auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();        auto ray_direction = pixel_sample - ray_origin;

        return ray(ray_origin, ray_direction);
                            
    }

    vec3 sample_square() const 
    {
        // Returns the vector to a random point in the [-.5,-.5]-[+.5,+.5] unit square.
        return vec3(random_double() - 0.5, random_double() - 0.5, 0);
    }

    point3 defocus_disk_sample() const 
    {
        // Returns a random point in the camera defocus disk.
        auto p = random_in_unit_disk();
        return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }

    color ray_color(const ray& r, int max_depth,const hittable& world) const 
    {
        if(max_depth <= 0)
            return color(0,0,0);

        hit_record rec;

        //randomly rendering

        // if (world.hit(r, interval(0.001, infinity), rec)) {
        //     vec3 direction = random_on_hemisphere(rec.normal);
        //     return 0.4 * ray_color(ray(rec.p, direction),max_depth-1, world);
        // }       //ok so what we are doing is when the ray hits first time, we record the point and normal in rec object. Then randomly find a unit vector, inside the sphere that is 
        //         //facing the same dir as normal (through dot product we check), then use this as direction, and start a ray from the point where it hit first time. then recurse it around, calculating the color
        //         //and adding to the final ray_color output.

        //rendering using lambertian reflection

        // if (world.hit(r, interval(0.001, infinity), rec)) {
        //     vec3 direction = rec.normal + random_unit_vector();
        //     return vec3(0.7,0.3,0.8) * ray_color(ray(rec.p, direction), max_depth-1, world);
        // }

        //rendering using material class objects

        if (world.hit(r, interval(0.001, infinity), rec)) 
        {
            ray scattered;
            color attenuation;
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_color(scattered, max_depth-1, world);
            return color(0,0,0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5*(unit_direction.y() + 1.0);
        return (1.0-a)*color(1.0, 1.0, 1.0) + a*color(0.5, 0.7, 1.0);
    }

};

#endif