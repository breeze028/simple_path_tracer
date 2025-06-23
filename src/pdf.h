#ifndef PDF_H
#define PDF_H
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

#include "hittable_list.h"
#include "onb.h"

class pdf {
public:
    virtual ~pdf() {
    }

    virtual double value(const vec3 &direction) const = 0;
    virtual vec3 generate() const = 0;
};

class sphere_pdf : public pdf {
public:
    sphere_pdf() {
    }

    double value(const vec3 &direction) const override {
        return 1 / (4 * pi);
    }

    vec3 generate() const override {
        return random_unit_vector();
    }
};

class cosine_pdf : public pdf {
public:
    cosine_pdf(const vec3 &w) :
        uvw(w) {
    }

    double value(const vec3 &direction) const override {
        auto cosine_theta = dot(unit_vector(direction), uvw.w());
        return std::fmax(0, cosine_theta / pi);
    }

    vec3 generate() const override {
        return uvw.transform(random_cosine_direction());
    }

private:
    onb uvw;
};

class phong_pdf : public pdf {
public:
    phong_pdf(const vec3 &w, double alpha, vec3 n) :
        alpha(interval(0.1, 1000.0).clamp(alpha)),
        n(n),
        uvw(w) {}

    double value(const vec3 &direction) const override {
        auto cosine_theta = dot(unit_vector(direction), uvw.w());
        cosine_theta = interval(0.0, 1.0).clamp(cosine_theta);
        return (alpha + 1) * std::pow(cosine_theta, alpha) / (2 * pi);
    }

    vec3 generate() const override {
        while (true) {
            auto phi = random_double() * 2 * pi;
            auto xi = std::max(1e-10, random_double());
            auto cos_theta = std::pow(xi, 1.0 / (alpha + 1));
            auto sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
            auto direction = uvw.transform(vec3(
                cos(phi) * sin_theta,
                sin(phi) * sin_theta,
                cos_theta
            ));
            if (dot(direction, n) > 0) return direction;
        }
    }

private:
    onb uvw;
    double alpha;
    vec3 n;
};

class hittable_pdf : public pdf {
public:
    hittable_pdf(const hittable &objects, const point3 &origin) :
        objects(objects), origin(origin) {
    }

    double value(const vec3 &direction) const override {
        return objects.pdf_value(origin, direction);
    }

    vec3 generate() const override {
        return objects.random(origin);
    }

private:
    const hittable &objects;
    point3 origin;
};

class mixture_pdf : public pdf {
public:
    mixture_pdf(shared_ptr<pdf> p0, shared_ptr<pdf> p1) {
        p[0] = p0;
        p[1] = p1;
    }

    double value(const vec3 &direction) const override {
        return 0.5 * p[0]->value(direction) + 0.5 * p[1]->value(direction);
    }

    vec3 generate() const override {
        if (random_double() < 0.5)
            return p[0]->generate();
        else
            return p[1]->generate();
    }

private:
    shared_ptr<pdf> p[2];
};

#endif
