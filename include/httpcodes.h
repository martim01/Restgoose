#ifndef PML_RESTGOOSE_HTTP_CODES_H_
#define PML_RESTGOOSE_HTTP_CODES_H_

namespace pml::restgoose::httpcodes
{
    const int kOk = 200;
    const int kCreated = 201;
    const int kAccepted = 202;
    const int kNoContent = 204;

    const int kMovedPermanently = 301;
    const int kFound = 302;
    const int kSeeOther = 303;
    const int kTemporaryRedirect = 307;
    const int kPermanentRedirect = 308;

    const int kBadRequest = 400;
    const int kUnauthorized = 401;
    const int kForbidden = 403;
    const int kNotFound = 404;
    const int kMethodNotAllowed = 405;

    const int kConflict = 409;
    const int kInternalServerError = 500;
    const int kNotImplemented = 501;
    const int kBadGateway = 502;
    const int kServiceUnavailable = 503;
    
} //namespace pml::restgoose::httpcodes

#endif //PML_RESTGOOSE_HTTP_CODES_H_
