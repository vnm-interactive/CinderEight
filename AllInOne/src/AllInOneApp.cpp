#include "cinder/app/AppNative.h"
#include "cinder/Surface.h"
#include "cinder/gl/Texture.h"
#include "cinder/Capture.h"
#include "cinder/Text.h"
#include "cinder/gl/gl.h"
#include "cinder/Utilities.h"

#include <boost/date_time/posix_time/posix_time.hpp>

using namespace ci;
using namespace ci::app;
using namespace std;

static const int WIDTH = 640, HEIGHT = 480;

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
	Surface		    mSurface, mCumulativeSurface;
    size_t          frameNum;
};

void AllInOneApp::setup()
{
	// list out the devices
	vector<Capture::DeviceRef> devices( Capture::getDevices() );
	for( vector<Capture::DeviceRef>::const_iterator deviceIt = devices.begin(); deviceIt != devices.end(); ++deviceIt ) {
		Capture::DeviceRef device = *deviceIt;
		console() << "Found Device " << device->getName() << " ID: " << device->getUniqueId() << std::endl;
        try {
            mCapture = Capture::create( 640, 480 );
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
			writeImage( getHomeDirectory() / ("Desktop/AllInOne-"+timestamp.str()+".png"), mCumulativeSurface );
        }
            break;
        default:
            break;
	}
}

void AllInOneApp::update()
{
	if( mCapture->checkNewFrame() ) {

        mSurface = mCapture->getSurface();
        
        if (frameNum > 0){
            float oneOverFrameNum = 1./(float)frameNum;
            Surface::Iter iter = mSurface.getIter( );
            Surface::Iter mCumulativeIter = mCumulativeSurface.getIter();
            while( iter.line() && mCumulativeIter.line()) {
                while( iter.pixel() && mCumulativeIter.pixel()) {
                    //avg(i) = (i-1)/i*avg(i-1) + x(i)/i;
                    mCumulativeIter.r() = ((frameNum-1) * mCumulativeIter.r() + iter.r()) * oneOverFrameNum;
                    mCumulativeIter.g() = ((frameNum-1) * mCumulativeIter.g() + iter.g()) * oneOverFrameNum;
                    mCumulativeIter.b() = ((frameNum-1) * mCumulativeIter.b() + iter.r()) * oneOverFrameNum;
                }
            }
            mTexture = gl::Texture::create(mCumulativeSurface);
        } else {
            mTexture = gl::Texture::create (mSurface);
            Area area = mSurface.getBounds();
            mCumulativeSurface = Surface( area.getWidth(), area.getHeight(), false );
            mCumulativeSurface.copyFrom(mSurface, area);
        }
        frameNum++;
    }
}

void AllInOneApp::draw()
{
	gl::enableAlphaBlending();
	gl::clear( Color::black() );
    
	float width = getWindowWidth();
	float height = width / ( WIDTH / (float)HEIGHT );
	float x = 0, y = ( getWindowHeight() - height ) / 2.0f;

    // draw the latest frame
    gl::color( Color::white() );
    if( mTexture)
        gl::draw( mTexture, Rectf( x, y, x + width, y + height ) );
    
}

CINDER_APP_NATIVE( AllInOneApp, RendererGl )