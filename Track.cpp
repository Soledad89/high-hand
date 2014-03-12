#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <iostream>
using namespace std;
using namespace cv;

#define VIDEO_DEVICE_NO 1
#define AREA_LIMIT 1500
#define ARC_LENGTH_LIMIT 30000
#define TRACE_LENGTH_LIMIT_LOW 50
#define TRACE_LENGTH_LIMIT_HIGH 300
#define PACE_THRESHOLD 30
#define START_DRAW 5

#define EPS 1e-8

#define DEBUG 1

//Algorithm libs
float SqrDis (Point &a, Point &b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

Point PolyCenter (vector<Point> &vt) {
    Point ret(0.0, 0.0);
#if DEBUG
    cout << "Num of Points = " << vt.size() << endl;
#endif
    for (int i = 0; i < vt.size(); i++) {
        ret.x += vt[i].x;
        ret.y += vt[i].y;
#if DEBUG
        cout << vt[i] << " ";
#endif
    }
#if DEBUG
    cout << endl;
#endif
    if (vt.size() > 0) {
        ret.x /= vt.size();
        ret.y /= vt.size();
    }
    return ret;
}

class Analyser {
public:
    Analyser() {
        count = 0;
    }
    ~Analyser() {}
    void clear() {
        count = 0;
        gesture.clear();
    }
    void judge(Point p) {
        if (count == 0)
            cur_point = p;
        else {
            //float d = (p.x - cur_point.x) * (p.x - cur_point.x) + (p.y - cur_point.y) * (p.y - cur_point.y);
            float d = SqrDis(p, cur_point);
            if (d > TRACE_LENGTH_LIMIT_LOW * TRACE_LENGTH_LIMIT_LOW && d < TRACE_LENGTH_LIMIT_HIGH * TRACE_LENGTH_LIMIT_HIGH) {
                if (fabs(p.x - cur_point.x) < EPS) {
                    if (p.y < cur_point.y)
                        gesture.push_back(NORTH);
                    else
                        gesture.push_back(SOUTH);
                } else {
                    float slope = (cur_point.y - p.y) / (cur_point.x - p.x);
                    if (cur_point.y > p.y) {
                        if (slope > 1 || slope < -1)
                            gesture.push_back(NORTH);
                        else if (cur_point.x > p.x)
                            gesture.push_back(WEST);
                        else
                            gesture.push_back(EAST);
                    } else {
                        if (slope > 1 || slope < -1)
                            gesture.push_back(SOUTH);
                        else if (cur_point.x > p.x)
                            gesture.push_back(WEST);
                        else
                            gesture.push_back(EAST);
                    }
                }
                cur_point = p;
            }
        }
        count++;

        if (gesture.size() > 1) {
            int sz = gesture.size() - 1;
            if (gesture[sz] == gesture[sz - 1])
                gesture.pop_back();
        }
    }
    void printGesture() {
        if (gesture.size() == 0) {
            return;
        }
        printf("Gesture: ");
        for (int i = 0; i < gesture.size(); i++) {
            if (gesture[i] == NORTH)
                printf("↑ ");
            else if (gesture[i] == EAST)
                printf("→ ");
            else if (gesture[i] == SOUTH)
                printf("↓ ");
            else
                printf("← ");
        }
        printf("\n");
    }
private:
    //N = 0, E = 1, S = 2, W = 3
    enum DIRECTION {NORTH, EAST, SOUTH, WEST};
    vector <DIRECTION> gesture;
    Point cur_point;
    int count;
};


class Tracker {
public:
    Tracker() {
        capture = NULL;
        this->frame_of_null = 0;
        this->last_trace_distance = -1.0;
        this->last_center = Point(-1.0, -1.0);
    }
    ~Tracker() {
        if (capture != NULL)
            capture->release();
    }
    bool InitSkinModel() {
        this->skin_model = Mat::zeros(Size(256, 256), CV_8UC1);
        ellipse(this->skin_model, Point(113, 155.6), Size(23.4, 15.2), 43.0, 0.0, 360.0, Scalar(255, 255, 255), -1);
        return true;
    }
    bool StartCamera() {
        if (capture != NULL)
            return false;
        capture = new VideoCapture(VIDEO_DEVICE_NO);
        if (capture->isOpened())
            return true;
        return false;
    }
    bool StopCamera() {
        if (capture != NULL) {
            capture->release();
            waitKey(1);
        }
        return true;
    }
    bool GetNextFrame() {
        if (!capture->read(this->src_img))
            return false;

        flip(src_img, src_img, 1);
        src_img.convertTo(src_img, CV_32FC3);
        medianBlur(src_img, src_img, 5);
        normalize(src_img, src_img, 1.0, 0.0, CV_MINMAX);
        return true;
    }

    void CleanTracking() {
        frame_of_null = 0;
        vpace.clear();
        analyser.clear();
        last_trace_distance = -1.0;
        this->last_center = Point(-1.0, -1.0);
    }
    void Display() {
        imshow("source", this->src_img);
        imshow("mask", this->mask);
        imshow("trace", this->trace);
    }

    bool GenerateBackground() {
        if (GetNextFrame() == false)
            return false;
        src_img.copyTo(background);
        src_img.copyTo(pre_frame);
        return true;
    }

    void SkinExtract() {
        src_img.convertTo(src_img, CV_8UC3, 255);
        mask = Mat::zeros(src_img.size(), CV_8UC1);

        Mat element = getStructuringElement(MORPH_RECT, Size(3, 3), Point(1, 1));
        erode(src_img, src_img, element);
        erode(src_img, src_img, element);
        dilate(src_img, src_img, element);
        Mat yuv;
        cvtColor(src_img, yuv, CV_BGR2YCrCb);
        for (int i = 0; i < src_img.cols; ++i) {
            for (int j = 0; j < src_img.rows; ++j) {
                Vec3b ycrcb = yuv.at<Vec3b>(j, i);
                if (skin_model.at<uchar>(ycrcb[1], ycrcb[2]) > 0)
                    mask.at<uchar>(j, i) = 255;
            }
        }

        erode(mask, mask, element);
        //erode(mask, mask, element);
        //erode(mask, mask, element);
        dilate(mask, mask, element);
        src_img.copyTo(src_img, mask);
    }

    void DrawTrace() {
        contours.clear();
        filter.clear();
        structure.clear();

        findContours(mask, contours, structure, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);

        //filter
        int max_area = 0, cur_area;
        Point cur_center, tmp_center;
        for (int i = 0; i < contours.size(); ++i) {
            convexHull(contours[i], contours[i]);

            //each points of the convex should not be located at the edge of frame
            bool flag = true;
            for (int j = 0; flag && j < contours[i].size(); j++) {
                if (contours[i][j].x <= 5 || contours[i][j].y <= 5)
                    flag = false;
            }
            if (!flag)
                continue;

            cur_area = fabs(contourArea(Mat(contours[i])));
            if (cur_area > AREA_LIMIT &&
                fabs(arcLength(Mat(contours[i]), true)) < ARC_LENGTH_LIMIT) {

                //find the most neighbor convex as hand
                if (!vpace.empty()) {
                    tmp_center = PolyCenter(contours[i]);
#if DEBUG
                    cout << "tmp center = " << tmp_center << endl << "cur center = " << cur_center << endl << "last center = " << last_center << endl;
#endif
                    if (i == 0 || SqrDis(cur_center, last_center) > SqrDis(tmp_center, last_center)) {
                        cur_center.x = tmp_center.x;
                        cur_center.y = tmp_center.y;
                        filter.clear();
                        filter.push_back(contours[i]);
                    }
                    continue;
                }

                //find the max convex as hand
                if (cur_area > max_area) {
                    filter.clear();
                    filter.push_back(contours[i]);
                    max_area = cur_area;
                }
            }
        }
        src_img.copyTo(trace);
        drawContours(trace, filter, -1, Scalar(255, 0, 0), 2);

        //draw convex center
        if (!filter.empty()) {
            float x = 0, y = 0;
/*            for (int i = 0; i < filter[0].size(); i++) {
                x += (float)filter[0][i].x;
                y += (float)filter[0][i].y;
#if DEBUG
                cout << "(" << filter[0][i].x << ", " << filter[0][i].y << ") ";
#endif
            }
#if DEBUG
            cout << endl;
#endif
            x /= filter[0].size();
            y /= filter[0].size();
*/
            cur_center = PolyCenter(filter[0]);
            last_center = cur_center;
            x = cur_center.x, y = cur_center.y;

            //find the longest finger
            float px, py, d = 500, td;
            for (int i = 0; i < filter[0].size(); i++) {
                //td = sqrt((filter[0][i].x - x) * (filter[0][i].x - x) + (filter[0][i].y - y) * (filter[0][i].y - y));
                td = filter[0][i].y;
                if (td < d) {
                    px = filter[0][i].x;
                    py = filter[0][i].y;
                    d = td;
                }
            }

            //judge whether (px, py) is the point we tracked in the last frame
            if (!vpace.empty()) {
                //td = (vpace.back().x - px) * (vpace.back().x - px) + (vpace.back().y - py) * (vpace.back().y - py);
                Point finger(px, py);
                td = SqrDis(vpace.back(), finger);
                if (td > TRACE_LENGTH_LIMIT_LOW * TRACE_LENGTH_LIMIT_LOW || (last_trace_distance > 64 && td > 16 * last_trace_distance)) {
                    frame_of_null++;
                    if (frame_of_null > PACE_THRESHOLD) {
                        CleanTracking();
                    } /*else {
                        for (int i = START_DRAW; i < vpace.size(); i++) {
                            line(trace, vpace[i - 1], vpace[i], Scalar(255, 255, 0), 2);
                        }
                    }*/
                    return;
                }
                last_trace_distance = td;
            }

            //draw the convex's center
            circle(trace, Point(x, y), 10, Scalar(0, 0, 255), 5);

            //draw the longest finger
            line(trace, Point(x, y), Point(px, py), Scalar(0, 255, 0), 2);

            //draw trace
            analyser.judge(Point(px, py));
            vpace.push_back(Point(px, py));
            frame_of_null = 0;
            Point cur, nxt;
            for (int i = START_DRAW; i < vpace.size() - 1; i++) {
                if (i == START_DRAW)
                    cur = vpace[i - 1];
                nxt.x = (cur.x + vpace[i].x + vpace[i + 1].x) / 3.0;
                nxt.y = (cur.y + vpace[i].y + vpace[i + 1].y) / 3.0;
                //line(trace, vpace[i - 1], vpace[i], Scalar(255, 255, 0), 2);
                line(trace, cur, nxt, Scalar(255, 255, 0), 2);
                cur = nxt;
            }
            analyser.printGesture();
        } else {
            frame_of_null++;
            if (frame_of_null > PACE_THRESHOLD) {
                CleanTracking();
            } else {
                for (int i = START_DRAW; i < vpace.size(); i++) {
                    line(trace, vpace[i - 1], vpace[i], Scalar(255, 255, 0), 2);
                }
            }
        }
    }

    void Run() {
        if (InitSkinModel() == false)
            return;
        if (StartCamera() == false)
            return;
        if (GenerateBackground() == false)
            return;
        while (GetNextFrame() == true) {
            SkinExtract();
            DrawTrace();
            Display();
            char key = (char)waitKey(1);
            if (key == 'q' || key == 'Q' || key == 27)
                break;
        }
        StopCamera();
        return;
    }
private:
    VideoCapture *capture;
    Mat src_img;

    //ellipse-skin-model
    Mat skin_model, mask;

    //background frame
    Mat background, pre_frame;

    //trace of hand
    vector< vector<Point> > contours;
    vector< vector<Point> > filter;
    vector<Vec4i> structure;
    Mat trace;
    vector<Point> vpace;
    int frame_of_null;
    double last_trace_distance;
    Point last_center;
    Analyser analyser;

};


int main() {
    Tracker tracker;
    tracker.Run();
    return 0;
}
