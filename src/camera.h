#ifndef CAMERA_H
#define CAMERA_H
//==============================================================================================
// Originally written in 2016 by Peter Shirley <ptrshrl@gmail.com>
//
// To the extent possible under law, the author(s) have dedicated all copyright and related and
// neighboring rights to this software to the public domain worldwide. This software is
// distributed without any warranty.
//
// You should have received a copy (see file COPYING.txt) of the CC0 Public Domain Dedication
// along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
//==============================================================================================

#include <chrono>
#include <vector>
#include <omp.h>
#include <iomanip>

#include "hittable.h"
#include "pdf.h"
#include "material.h"

class camera {
public:
    double aspect_ratio = 1.0;  // Ratio of image width over height
    int image_width = 100;      // Rendered image width in pixel count
    int samples_per_pixel = 10; // Count of random samples for each pixel
    int max_depth = 10;         // Maximum number of ray bounces into scene
    color background;           // Scene background color

    double vfov = 90;                  // Vertical view angle (field of view)
    point3 lookfrom = point3(0, 0, 0); // Point camera is looking from
    point3 lookat = point3(0, 0, -1);  // Point camera is looking at
    vec3 vup = vec3(0, 1, 0);          // Camera-relative "up" direction

    double defocus_angle = 0; // Variation angle of rays through each pixel
    double focus_dist = 10;   // Distance from camera lookfrom point to plane of perfect focus

    enum class RenderMode {
        BSDF_SAMPLING,
        MIXTURE_SAMPLING,
        NEE,
        MIS
    } render_mode = RenderMode::MIS;

    void render(const hittable &world, const hittable &lights) {
        auto start = std::chrono::steady_clock::now();

        initialize();

        std::vector<std::vector<color>> img(image_height, std::vector<color>(image_width));

//#pragma omp parallel for schedule(dynamic)
        for (int j = 0; j < image_height; j++) {
            for (int i = 0; i < image_width; i++) {
                double x = 0, y = 0, z = 0;
                for (int s_j = 0; s_j < sqrt_spp; s_j++) {
                    for (int s_i = 0; s_i < sqrt_spp; s_i++) {
                        ray r = get_ray(i, j, s_i, s_j);
                        color c;
                        switch (render_mode) {
                            case RenderMode::BSDF_SAMPLING:
                                c = ray_color_1(r, max_depth, world, lights);
                                break;
                            case RenderMode::MIXTURE_SAMPLING:
                                c = ray_color_2(r, max_depth, world, lights);
                                break;
                            case RenderMode::NEE:
                                c = ray_color_3(r, max_depth, world, lights, true);
                                break;
                            case RenderMode::MIS:
                                c = ray_color_4(r, max_depth, world, lights, 1.0);
                                break;
                        }
                        x += c.x();
                        y += c.y();
                        z += c.z();
                    }                  
                }
                img[j][i] = color(x, y, z) * pixel_samples_scale;              
            }
        }

        auto end = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(end - start).count();

        std::clog << "Time: " << std::fixed << std::setprecision(3) << secs << " (s)\n";

        // Output the image
        std::cout << "P3\n"
                  << image_width << ' ' << image_height << "\n255\n";
        for (int j = 0; j < image_height; j++) {
            for (int i = 0; i < image_width; i++) {
                write_color(std::cout, img[j][i]);
            }
        }
    }

private:
    int image_height;           // Rendered image height
    double pixel_samples_scale; // Color scale factor for a sum of pixel samples
    int sqrt_spp;               // Square root of number of samples per pixel
    double recip_sqrt_spp;      // 1 / sqrt_spp
    point3 center;              // Camera center
    point3 pixel00_loc;         // Location of pixel 0, 0
    vec3 pixel_delta_u;         // Offset to pixel to the right
    vec3 pixel_delta_v;         // Offset to pixel below
    vec3 u, v, w;               // Camera frame basis vectors
    vec3 defocus_disk_u;        // Defocus disk horizontal radius
    vec3 defocus_disk_v;        // Defocus disk vertical radius

    void initialize() {
        image_height = int(image_width / aspect_ratio);
        image_height = (image_height < 1) ? 1 : image_height;

        sqrt_spp = int(std::sqrt(samples_per_pixel));
        pixel_samples_scale = 1.0 / (sqrt_spp * sqrt_spp);
        recip_sqrt_spp = 1.0 / sqrt_spp;

        center = lookfrom;

        // Determine viewport dimensions.
        auto theta = degrees_to_radians(vfov);
        auto h = std::tan(theta / 2);
        auto viewport_height = 2 * h * focus_dist;
        auto viewport_width = viewport_height * (double(image_width) / image_height);

        // Calculate the u,v,w unit basis vectors for the camera coordinate frame.
        w = unit_vector(lookfrom - lookat);
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        // Calculate the vectors across the horizontal and down the vertical viewport edges.
        vec3 viewport_u = viewport_width * u;   // Vector across viewport horizontal edge
        vec3 viewport_v = viewport_height * -v; // Vector down viewport vertical edge

        // Calculate the horizontal and vertical delta vectors from pixel to pixel.
        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        // Calculate the location of the upper left pixel.
        auto viewport_upper_left = center - (focus_dist * w) - viewport_u / 2 - viewport_v / 2;
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        // Calculate the camera defocus disk basis vectors.
        auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }

    ray get_ray(int i, int j, int s_i, int s_j) const {
        // Construct a camera ray originating from the defocus disk and directed at a randomly
        // sampled point around the pixel location i, j for stratified sample square s_i, s_j.

        auto offset = sample_square_stratified(s_i, s_j);
        auto pixel_sample = pixel00_loc
                            + ((i + offset.x()) * pixel_delta_u)
                            + ((j + offset.y()) * pixel_delta_v);

        auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
        auto ray_direction = pixel_sample - ray_origin;
        auto ray_time = random_double();

        return ray(ray_origin, ray_direction, ray_time);
    }

    vec3 sample_square_stratified(int s_i, int s_j) const {
        // Returns the vector to a random point in the square sub-pixel specified by grid
        // indices s_i and s_j, for an idealized unit square pixel [-.5,-.5] to [+.5,+.5].

        auto px = ((s_i + random_double()) * recip_sqrt_spp) - 0.5;
        auto py = ((s_j + random_double()) * recip_sqrt_spp) - 0.5;

        return vec3(px, py, 0);
    }

    vec3 sample_square() const {
        // Returns the vector to a random point in the [-.5,-.5]-[+.5,+.5] unit square.
        return vec3(random_double() - 0.5, random_double() - 0.5, 0);
    }

    vec3 sample_disk(double radius) const {
        // Returns a random point in the unit (radius 0.5) disk centered at the origin.
        return radius * random_in_unit_disk();
    }

    point3 defocus_disk_sample() const {
        // Returns a random point in the camera defocus disk.
        auto p = random_in_unit_disk();
        return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }

    // bsdf sampling
    color ray_color_1(const ray &r, int depth, const hittable &world, const hittable &lights)
        const {
        // If we've exceeded the ray bounce limit, no more light is gathered.
        if (depth <= 0)
            return color(0, 0, 0);

        hit_record rec;

        // If the ray hits nothing, return the background color.
        if (!world.hit(r, interval(0.001, infinity), rec))
            return background;

        scatter_record srec;
        color color_from_emission = rec.mat->emitted(r, rec, rec.u, rec.v, rec.p);

        if (!rec.mat->scatter(r, rec, srec))
            return color_from_emission;

        if (srec.skip_pdf) {
            return srec.attenuation * ray_color_1(srec.skip_pdf_ray, depth - 1, world, lights);
        }

        vec3 dir = srec.pdf_ptr->generate();
        if (dir.length_squared() < 0.0001) {
            return color_from_emission; // Avoid invalid direction
        }
        ray scattered = ray(rec.p, dir, r.time());
        auto pdf_value = srec.pdf_ptr->value(scattered.direction());

        double scattering_pdf = rec.mat->scattering_pdf(r, rec, scattered);

        color sample_color = ray_color_1(scattered, depth - 1, world, lights);
        color color_from_scatter =
            (srec.attenuation * scattering_pdf * sample_color) / pdf_value;

        return color_from_emission + color_from_scatter;
    }

    // path tracing with mixture sampling
    color ray_color_2(const ray &r, int depth, const hittable &world, const hittable &lights)
        const {
        // If we've exceeded the ray bounce limit, no more light is gathered.
        if (depth <= 0)
            return color(0, 0, 0);

        hit_record rec;

        // If the ray hits nothing, return the background color.
        if (!world.hit(r, interval(0.001, infinity), rec))
            return background;

        scatter_record srec;
        color color_from_emission = rec.mat->emitted(r, rec, rec.u, rec.v, rec.p);

        if (!rec.mat->scatter(r, rec, srec))
            return color_from_emission;

        if (srec.skip_pdf) {
            return srec.attenuation * ray_color_2(srec.skip_pdf_ray, depth - 1, world, lights);
        }

        auto light_ptr = make_shared<hittable_pdf>(lights, rec.p);
        mixture_pdf p(light_ptr, srec.pdf_ptr);

        ray scattered = ray(rec.p, p.generate(), r.time());
        auto pdf_value = p.value(scattered.direction());

        double scattering_pdf = rec.mat->scattering_pdf(r, rec, scattered);

        color sample_color = ray_color_2(scattered, depth - 1, world, lights);
        color color_from_scatter =
            (srec.attenuation * scattering_pdf * sample_color) / pdf_value;

        return color_from_emission + color_from_scatter;
    }

    // path tracing with NEE
    color ray_color_3(const ray &r, int depth, const hittable &world, const hittable &lights, bool includeLe)
        const {
        hit_record rec;

        // If the ray hits nothing, return the background color.
        if (!world.hit(r, interval(0.001, infinity), rec))
            return background;

        color Le = includeLe ? rec.mat->emitted(r, rec, rec.u, rec.v, rec.p) : color(0, 0, 0);

        // end one light path (too many vertices or NEE)
        if (depth <= 0)
            return Le;

        scatter_record srec;

        if (!rec.mat->scatter(r, rec, srec))
            return Le;

        if (srec.skip_pdf) {
            return srec.attenuation * ray_color_3(srec.skip_pdf_ray, depth - 1, world, lights, true);
        }

        // NEE
        auto light_ptr = make_shared<hittable_pdf>(lights, rec.p);
        ray light_ray = ray(rec.p, light_ptr->generate(), r.time());
        color brdf = srec.attenuation * rec.mat->scattering_pdf(r, rec, light_ray);
        double pdf_light = light_ptr->value(light_ray.direction());
        color Ldir = brdf * ray_color_3(light_ray, 0, world, lights, true) / pdf_light;

        // BSDF
        ray bsdf_ray = ray(rec.p, srec.pdf_ptr->generate(), r.time());
        color bsdf = srec.attenuation * rec.mat->scattering_pdf(r, rec, bsdf_ray);
        double pdf_bsdf = srec.pdf_ptr->value(bsdf_ray.direction());
        color Lind = bsdf * ray_color_3(bsdf_ray, depth - 1, world, lights, false) / pdf_bsdf;

        return Le + Ldir + Lind;
    }

    // path tracing with MIS
    color ray_color_4(const ray &r, int depth, const hittable &world, const hittable &lights, double Leweight)
        const {
        hit_record rec;

        // If the ray hits nothing, return the background color.
        if (!world.hit(r, interval(0.001, infinity), rec))
            return background;

        color Le = Leweight * rec.mat->emitted(r, rec, rec.u, rec.v, rec.p);

        // end one light path (too many vertices or NEE)
        if (depth <= 0)
            return Le;

        scatter_record srec;

        if (!rec.mat->scatter(r, rec, srec))
            return Le;

        if (srec.skip_pdf) {
            return srec.attenuation * ray_color_4(srec.skip_pdf_ray, depth - 1, world, lights, true);
        }

        // NEE
        auto light_ptr = make_shared<hittable_pdf>(lights, rec.p);
        ray light_ray = ray(rec.p, light_ptr->generate(), r.time());
        color brdf = srec.attenuation * rec.mat->scattering_pdf(r, rec, light_ray);
        double pdf_light = light_ptr->value(light_ray.direction());
        double pdf_light_bsdf = srec.pdf_ptr->value(light_ray.direction());
        //double weight_light = pdf_light / (pdf_light + pdf_light_bsdf);
        double weight_light = pow(pdf_light, 2) / (pow(pdf_light, 2) + pow(pdf_light_bsdf, 2));
        color Ldir = brdf * ray_color_4(light_ray, 0, world, lights, weight_light) / pdf_light;

        // BSDF
        vec3 dir = srec.pdf_ptr->generate();
        ray bsdf_ray = ray(rec.p, dir, r.time());
        color bsdf = srec.attenuation * rec.mat->scattering_pdf(r, rec, bsdf_ray);
        double pdf_bsdf = srec.pdf_ptr->value(bsdf_ray.direction());
        double pdf_bsdf_light = light_ptr->value(bsdf_ray.direction());
        //double weight_bsdf = pdf_bsdf / (pdf_bsdf + pdf_bsdf_light);
        double weight_bsdf = pow(pdf_bsdf, 2) / (pow(pdf_bsdf, 2) + pow(pdf_bsdf_light, 2));
        color Lind = bsdf * ray_color_4(bsdf_ray, depth - 1, world, lights, weight_bsdf) / pdf_bsdf;

        return Le + Ldir + Lind;
    }
};

#endif
