#include "cinder/app/AppNative.h"
#include "cinder/Surface.h"
#include "cinder/gl/Texture.h"
#include "cinder/Capture.h"
#include "cinder/Text.h"
#include "cinder/gl/gl.h"
#include "cinder/Utilities.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/format.hpp>

using namespace ci;
using namespace ci::app;
using namespace std;

static const int WIDTH = 640, HEIGHT = 480;
static const float ONE_OVER_255 = 1.0/255.;
static const int AVERAGE_TYPE = 1;
static const int SCREEN_TYPE = 2;

#define toDigit(c) (c-'0')

class AllInOneApp : public AppNative {
public:
	void setup();
	void keyDown( KeyEvent event );
	void update();
	void draw();
	
private:
	CaptureRef		mCapture;
	gl::TextureRef	mTexture;
	gl::TextureRef	mNameTexture;
	Surface		    mSurface, mPrevSurface;
    Surface32f      mCumulativeSurface32f;
    gl::Texture::Format hdrFormat;
    size_t          frameNum;
    Color           averageColor;
    int type;
    
    void initAverage();
    void computeAverage();
    void initScreen();
    void computeScreen();
    
    string getBlendMode();
};

void AllInOneApp::setup()
{
	// list out the devices
	vector<Capture::DeviceRef> devices( Capture::getDevices() );
	for( vector<Capture::DeviceRef>::const_iterator deviceIt = devices.begin(); deviceIt != devices.end(); ++deviceIt ) {
		Capture::DeviceRef device = *deviceIt;
		console() << "Found Device " << device->getName() << " ID: " << device->getUniqueId() << std::endl;
        try {
            mCapture = Capture::create( WIDTH, HEIGHT );
            mCapture->start();
            
            TextLayout layout;
            layout.setFont( Font( "Arial", 24 ) );
            layout.setColor( Color( 1, 1, 1 ) );
            layout.addLine( device->getName() );
            mNameTexture = gl::Texture::create( layout.render( true ) ) ;
        }
        catch( ... ) {
            console() << "Failed to initialize capture" << std::endl;
        }
	}
    frameNum = 0;
    type = AVERAGE_TYPE;
    hdrFormat.setInternalFormat(GL_FLOAT);
    getWindow()->setTitle("All In One by eight_io");
}

void AllInOneApp::keyDown( KeyEvent event )
{
    switch( event.getChar() ) {
        case 'f': setFullScreen( ! isFullScreen() ); break;
        case ' ':
            mCapture->isCapturing() ? mCapture->stop() : mCapture->start();
            break;
		case 's':
        {
            boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
            std::stringstream timestamp;
            timestamp << now;
            writeImage( getHomeDirectory() / ("Desktop/AllInOne-"+timestamp.str()+".png"), mCumulativeSurface32f );
        }
            break;
        case 'r':
            frameNum = 0;
        default:
            if (isdigit(event.getChar())){
                int newType = toDigit(event.getChar());
                if (newType != type && (newType >=AVERAGE_TYPE && newType <= SCREEN_TYPE)){
                    type = newType;
                    frameNum = 0;
                }
            }
            break;
	}
}

void AllInOneApp::initAverage(){
    Area area = mSurface.getBounds();
    mCumulativeSurface32f = Surface32f( area.getWidth(), area.getHeight(), false );
    auto cumIter = mCumulativeSurface32f.getIter();
    auto surfaceIter = mSurface.getIter();
    while(cumIter.line() && surfaceIter.line()){
        while(cumIter.pixel() && surfaceIter.pixel()){
            cumIter.r() = surfaceIter.r() * ONE_OVER_255;
            cumIter.g() = surfaceIter.g() * ONE_OVER_255;
            cumIter.b() = surfaceIter.b() * ONE_OVER_255;
        }
    }
}

void AllInOneApp::computeAverage(){
    if (frameNum == 0) {
        initAverage();
        return;
    }
    
    float oneOverFrameNum = 1./(float)frameNum;
    auto iter = mSurface.getIter( );
    auto mCumulativeIter = mCumulativeSurface32f.getIter();
    while( iter.line() && mCumulativeIter.line()) {
        while( iter.pixel() && mCumulativeIter.pixel()) {
            //avg(i) = (i-1)/i*avg(i-1) + x(i)/i;
            mCumulativeIter.r() = ((frameNum-1) * mCumulativeIter.r() + iter.r()*ONE_OVER_255) * oneOverFrameNum;
            mCumulativeIter.g() = ((frameNum-1) * mCumulativeIter.g() + iter.g()*ONE_OVER_255) * oneOverFrameNum;
            mCumulativeIter.b() = ((frameNum-1) * mCumulativeIter.b() + iter.b()*ONE_OVER_255) * oneOverFrameNum;
        }
    }
    averageColor = mCumulativeSurface32f.areaAverage(mSurface.getBounds());
}

void AllInOneApp::initScreen(){
    Area area = mSurface.getBounds();
    mCumulativeSurface32f = Surface32f( area.getWidth(), area.getHeight(), false );
    
    mPrevSurface = Surface(area.getWidth(), area.getHeight(),false);
    mPrevSurface.copyFrom(mSurface, mSurface.getBounds());
    
    auto prevIter = mPrevSurface.getIter();
    auto cumIter = mCumulativeSurface32f.getIter();
    
    while( prevIter.line()&&cumIter.line()) {
        while( prevIter.pixel() && cumIter.pixel()) {
            cumIter.r() = prevIter.r() * ONE_OVER_255;
            cumIter.g() = prevIter.g() * ONE_OVER_255;
            cumIter.b() = prevIter.b() * ONE_OVER_255;
        }
    }
}

void AllInOneApp::computeScreen(){
    

    if (frameNum == 0) {
        initScreen();
        return;
    }
    
    //apply screen blending to previous surface
    auto iter = mSurface.getIter( );
    auto prevIter = mPrevSurface.getIter();
    while( iter.line() && prevIter.line()) {
        while( iter.pixel() && prevIter.pixel()) {
            //result = one - (one - a) * (one - b);
            prevIter.r() = 255 - (255 - prevIter.r()) * (255 - iter.r()) * ONE_OVER_255;
            prevIter.g() = 255 - (255 - prevIter.g()) * (255 - iter.g()) * ONE_OVER_255;
            prevIter.b() = 255 - (255 - prevIter.b()) * (255 - iter.b()) * ONE_OVER_255;
        }
    }
    
    //accumulate screen blending
    float oneOverFrameNum = 1./(float)frameNum;
    prevIter = mPrevSurface.getIter( );
    auto mCumulativeIter = mCumulativeSurface32f.getIter();
    while( prevIter.line() && mCumulativeIter.line()) {
        while( prevIter.pixel() && mCumulativeIter.pixel()) {
            //avg(i) = (i-1)/i*avg(i-1) + x(i)/i;
            mCumulativeIter.r() = ((frameNum-1) * mCumulativeIter.r() + prevIter.r()*ONE_OVER_255) * oneOverFrameNum;
            mCumulativeIter.g() = ((frameNum-1) * mCumulativeIter.g() + prevIter.g()*ONE_OVER_255) * oneOverFrameNum;
            mCumulativeIter.b() = ((frameNum-1) * mCumulativeIter.b() + prevIter.b()*ONE_OVER_255) * oneOverFrameNum;
        }
    }

    averageColor = mCumulativeSurface32f.areaAverage(mSurface.getBounds());
    
    //retain current surface for next iteration
    mPrevSurface.copyFrom(mSurface, mSurface.getBounds());
}

void AllInOneApp::update()
{
    bool isUpdated = false;
	if ( mCapture->checkNewFrame() ) {
        mSurface = mCapture->getSurface();
        isUpdated = true;
    }
    
    if (!isUpdated) return;
    switch (type) {
        case AVERAGE_TYPE:
            computeAverage();
            break;
        case SCREEN_TYPE:
            computeScreen();
            break;
        default:
            break;
    }
    mTexture = gl::Texture::create (mCumulativeSurface32f, hdrFormat);
    frameNum++;
}

void AllInOneApp::draw()
{
	//gl::enableAlphaBlending();
	gl::clear( Color::black() );
    
    // draw the latest frame
    gl::color( Color::white() );
    if( mTexture)
        gl::draw( mTexture, Rectf( 0, 0, getWindowWidth(), getWindowHeight()) );
    
    gl::drawString("color "+ boost::str(boost::format("%.3f") % averageColor.length())+ " "+ toString(getFrameRate())+" FPS " +"Blend: "+getBlendMode(), Vec2f(5.0f, 5.0f));

}

string AllInOneApp::getBlendMode(){
    switch (type) {
        case AVERAGE_TYPE:
            return "Average";
            break;
        case SCREEN_TYPE:
            return "Screen";
            break;
        default:
            return "UNKNOWN";
            break;
    }
}

CINDER_APP_NATIVE( AllInOneApp, RendererGl )