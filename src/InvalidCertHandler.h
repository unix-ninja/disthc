#ifndef NetSSL_myCertificateHandler_INCLUDED
#define NetSSL_myCertificateHandler_INCLUDED


#include "Poco/Net/NetSSL.h"
#include "Poco/Net/InvalidCertificateHandler.h"


//namespace Poco {
//namespace Net {


class NetSSL_API myCertificateHandler: public Poco::Net::InvalidCertificateHandler
        /// A myCertificateHandler is invoked whenever an error occurs verifying the certificate.
        /// 
        /// The certificate is printed to stdout and the user is asked via console if he wants to accept it.
{
public:
		myCertificateHandler();
		
        myCertificateHandler(bool handleErrorsOnServerSide);
                /// Creates the myCertificateHandler.

        virtual ~myCertificateHandler();
                /// Destroys the myCertificateHandler.

        void onInvalidCertificate(const void* pSender, Poco::Net::VerificationErrorArgs& errorCert);
                /// Prints the certificate to stdout and waits for user input on the console
                /// to decide if a certificate should be accepted/rejected.
};


myCertificateHandler::myCertificateHandler(): InvalidCertificateHandler(false)
{
	
}


myCertificateHandler::myCertificateHandler(bool server): InvalidCertificateHandler(server)
{
}


myCertificateHandler::~myCertificateHandler()
{
}


void myCertificateHandler::onInvalidCertificate(const void*, Poco::Net::VerificationErrorArgs& errorCert)
{
		errorCert.setIgnoreError(true);
		return;
		
        const Poco::Net::X509Certificate& aCert = errorCert.certificate();
        std::cout << "\n";
        std::cout << "WARNING: Certificate verification failed\n";
        std::cout << "----------------------------------------\n";
        std::cout << "Issuer Name:  " << aCert.issuerName() << "\n";
        std::cout << "Subject Name: " << aCert.subjectName() << "\n\n";
        std::cout << "The certificate yielded the error: " << errorCert.errorMessage() << "\n\n";
        std::cout << "The error occurred in the certificate chain at position " << errorCert.errorDepth() << "\n";
        std::cout << "Accept the certificate (y,n)? ";
        char c;
        std::cin >> c;
        if (c == 'y' || c == 'Y')
                errorCert.setIgnoreError(true);
        else
                errorCert.setIgnoreError(false);
}

//} } // namespace Poco::Net


#endif // NetSSL_myCertificateHandler_INCLUDED
