#include "cinder/app/AppNative.h"
#include "cinder/Surface.h"
#include "cinder/gl/Texture.h"
#include "cinder/Capture.h"
#include "cinder/Text.h"
#include "cinder/gl/gl.h"
#include "cinder/Utilities.h"

#include "Avf.h"

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
    void fileDrop( ci::app::FileDropEvent event );
    void prepareSettings(Settings *settings);
	
private:
	CaptureRef		mCapture;
	gl::TextureRef	mAccumTexture, mRealTimeTexture;
	gl::TextureRef	mNameTexture;
	Surface		    mSurface, mPrevSurface;

#if defined (CINDER_COCOA_TOUCH)
    Surface iosSurface;
#endif
    
    Surface32f      mCumulativeSurface32f;
    gl::Texture::Format hdrFormat;
    Font            mFont;
    size_t          frameNum;
    int type;
    bool doRecord;

    avf::MovieSurfaceRef mMovie;
    
    void initAverage(bool reset = false);
    void computeAverage();

    void initScreen(bool reset = false);
    void computeScreen();
    
    string getBlendMode();
};

void AllInOneApp::prepareSettings(Settings *settings){
    settings->disableFrameRate();
}

void AllInOneApp::setup()
{
	// list out the devices
	vector<Capture::DeviceRef> devices( Capture::getDevices() );
	for( vector<Capture::DeviceRef>::const_iterator deviceIt = devices.begin(); deviceIt != devices.end(); ++deviceIt ) {
		Capture::DeviceRef device = *deviceIt;
		console() << "Found Device " << device->getName() << " " << std::endl;
	}
    
    try {
        mCapture = Capture::create( WIDTH, HEIGHT );
        mCapture->start();
        
        TextLayout layout;
        layout.setFont( Font( "Arial", 24 ) );
        layout.setColor( Color( 1, 1, 1 ) );
       // layout.addLine( device->getName() );
        //mNameTexture = gl::Texture::create( layout.render( true ) ) ;
    }
    catch( ... ) {
        console() << "Failed to initialize capture" << std::endl;
    }
    
    frameNum = 0;
    type = SCREEN_TYPE;
#if defined ( CINDER_MAC )
    hdrFormat.setInternalFormat(GL_RGBA32F_ARB);
#else
    hdrFormat.setInternalFormat(GL_FLOAT);
#endif
    getWindow()->setTitle("All In One by eight_io");
    mFont = Font( "Helvetica", 12.0f );
    gl::disableVerticalSync();
    gl::enableAlphaBlending();
    
    doRecord = true;
}

void AllInOneApp::fileDrop( ci::app::FileDropEvent event ){
#if defined ( CINDER_MAC )
    fs::path moviePath = event.getFile(0);
	if( moviePath.empty() ) return;
    
    try {
		// load up the movie, set it to loop, and begin playing
        mMovie = avf::MovieSurface::create( moviePath );
		mMovie->setLoop(true);
        mMovie->play();
        mMovie->setRate(1.);
        mCapture -> stop();
        frameNum = 0;
	}
	catch( ... ) {
		console() << "Unable to load the movie." << std::endl;
		mMovie.reset();
		mAccumTexture.reset();
	}
#endif
}

void AllInOneApp::keyDown( KeyEvent event )
{
    switch( event.getChar() ) {
        case 'f': setFullScreen( ! isFullScreen() ); break;
        case 'r':
            //mCapture->isCapturing() ? mCapture->stop() : mCapture->start();
            doRecord = !doRecord;
            break;
		case 's':
        {
            boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
            std::stringstream timestamp;
            timestamp << now;
            writeImage( getHomeDirectory() / ("Desktop/AllInOne-"+timestamp.str()+".png"), mCumulativeSurface32f );
        }
            break;
        case ' ':
            frameNum = 0;
        default:
            if (isdigit(event.getChar())){
                int newType = toDigit(event.getChar());
                if (newType != type && (newType >=AVERAGE_TYPE && newType <= SCREEN_TYPE)){
                    type = newType;
                    
                    switch (type) {
                        case AVERAGE_TYPE:
                            initAverage();
                            break;
                        case SCREEN_TYPE:
                            initScreen();
                            break;
                        default:
                            break;
                    }
                }
            }
            break;
	}
}

void AllInOneApp::initAverage(bool reset){
    if (!reset) return;
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
        initAverage(true);
        return;
    }
    
    if (!doRecord) return;
    
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
}

void AllInOneApp::initScreen(bool reset){
    Area area = mSurface.getBounds();

    mPrevSurface = Surface(area.getWidth(), area.getHeight(),false);
    mPrevSurface.copyFrom(mSurface, mSurface.getBounds());
    
    auto prevIter = mPrevSurface.getIter();
    
    if (!reset) return;
    mCumulativeSurface32f = Surface32f( area.getWidth(), area.getHeight(), false );
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
    if (!doRecord) return;
    
    if (frameNum == 0) {
        initScreen(true);
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

    if (!isUpdated && mMovie && mMovie->checkNewFrame()){
        mSurface = mMovie->getSurface();
        isUpdated = true;
    }
    
    if (isUpdated) {
        mRealTimeTexture = gl::Texture::create(mSurface);
    } else {
        return;
    }
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
#if defined (CINDER_COCOA_TOUCH)
    
    Area area = mSurface.getBounds();
    Surface iosSurface = Surface(area.getWidth(), area.getHeight(), false);
    auto iosIt = iosSurface.getIter();
    auto cumIt = mCumulativeSurface32f.getIter();

    while( cumIt.line() && iosIt.line()) {
        while( cumIt.pixel() && iosIt.pixel()) {
            iosIt.r() = cumIt.r()*255;
            iosIt.g() = cumIt.g()*255;
            iosIt.b() = cumIt.b()*255;
        }
    }
    mAccumTexture = gl::Texture::create(iosSurface);
#elif defined (CINDER_MAC)
    mAccumTexture = gl::Texture::create (mCumulativeSurface32f, hdrFormat);
#endif
    
    if (doRecord)     frameNum++;
}

void AllInOneApp::draw()
{
	gl::clear( Color::black() );
	gl::setMatricesWindow( getWindowWidth(), getWindowHeight() );
#if defined( CINDER_COCOA_TOUCH )
    //change iphone to landscape orientation
    gl::rotate( 90.0f );
    gl::translate( 0.0f, -getWindowWidth() );
    
    Rectf flippedBounds( 0.0f, 0.0f, getWindowHeight(), getWindowWidth() );
    if( mAccumTexture)
        gl::draw( mAccumTexture, flippedBounds );
    if (mRealTimeTexture){
        gl::draw(mRealTimeTexture,Rectf((getWindowWidth() - 100), 0, getWindowWidth(),100/mAccumTexture->getAspectRatio()));
    }
#else
    if( mAccumTexture)
        gl::draw( mAccumTexture, Rectf( 0, 0, getWindowWidth(), getWindowHeight()) );
    
    if (mRealTimeTexture){
        gl::draw(mRealTimeTexture,Rectf((getWindowWidth() - 100), 0, getWindowWidth(),100/mAccumTexture->getAspectRatio()));
    }

#endif
    
    gl::drawString(" FPS: "+ toString(getFrameRate())+"    Blend: "+getBlendMode()+ "    Record: "+(doRecord ? " Yes":" No"), Vec2f(5.0f, 5.0f),Color::white(),mFont);

    glPopMatrix();
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
