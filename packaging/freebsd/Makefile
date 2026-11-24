PORTNAME=	spectral-browser
DISTVERSION=	1.3.4
CATEGORIES=	www
MASTER_SITES=	# none
DISTFILES=	# none
EXTRACT_ONLY=	# none

MAINTAINER=	ganymedepublishingillinois@gmail.com
COMMENT=	Privacy-minded GTK/WebKit web browser
WWW=		https://github.com/vmphil/spectral-browser

LICENSE=	GPLv3+
LICENSE_FILE=	${WRKSRC}/LICENSE

# Optional ports-tree path. The package you can pkg-add is built by
# packaging/freebsd/build-pkg.sh. To use this skeleton, copy packaging/freebsd
# to /usr/ports/www/spectral-browser and point SPECTRAL_SRC at a checkout
# that already contains the FreeBSD changes (this branch is not on a release tag).
#
#   cd /usr/ports/www/spectral-browser
#   make SPECTRAL_SRC=/path/to/spectral-browser package

BUILD_DEPENDS=	go:lang/go \
		pkgconf:devel/pkgconf
LIB_DEPENDS=	libgtk-3.so:x11-toolkits/gtk30 \
		libwebkit2gtk-4.1.so:www/webkit2-gtk@41 \
		libjavascriptcoregtk-4.1.so:www/webkit2-gtk@41 \
		libsoup-3.0.so:devel/libsoup3

USES=		compiler:c11 pkgconfig

.include <bsd.port.pre.mk>

.if !defined(SPECTRAL_SRC)
IGNORE=		set SPECTRAL_SRC to a spectral-browser checkout; see packaging/freebsd/README.md
.endif

do-extract:
	${MKDIR} ${WRKDIR}
	${RLN} ${SPECTRAL_SRC} ${WRKSRC}

do-build:
	cd ${WRKSRC} && ${SETENV} CGO_ENABLED=1 \
		GOPROXY="$${GOPROXY:-https://proxy.golang.org,direct}" \
		go build -trimpath -ldflags "-s -w" -o ${WRKDIR}/${PORTNAME} .
	cd ${WRKSRC} && ${CC} -shared -fPIC -O2 -Wall \
		-o ${WRKDIR}/libspectral-ext.so ext/spectral-ext.c \
		$$(pkg-config --cflags --libs webkit2gtk-web-extension-4.1)

do-install:
	${INSTALL_PROGRAM} ${WRKDIR}/${PORTNAME} ${STAGEDIR}${PREFIX}/bin/${PORTNAME}
	${MKDIR} ${STAGEDIR}${PREFIX}/lib/${PORTNAME}
	${INSTALL_LIB} ${WRKDIR}/libspectral-ext.so \
		${STAGEDIR}${PREFIX}/lib/${PORTNAME}/
	${MKDIR} ${STAGEDIR}${PREFIX}/share/applications \
		${STAGEDIR}${PREFIX}/share/icons/hicolor/256x256/apps \
		${STAGEDIR}${PREFIX}/share/icons/hicolor/512x512/apps \
		${STAGEDIR}${PREFIX}/share/pixmaps \
		${STAGEDIR}${PREFIX}/share/doc/${PORTNAME}
	${INSTALL_DATA} ${WRKSRC}/packaging/freebsd/spectral-browser.desktop \
		${STAGEDIR}${PREFIX}/share/applications/${PORTNAME}.desktop
	${INSTALL_DATA} ${WRKSRC}/packaging/freebsd/icons/256x256/${PORTNAME}.png \
		${STAGEDIR}${PREFIX}/share/icons/hicolor/256x256/apps/${PORTNAME}.png
	${INSTALL_DATA} ${WRKSRC}/packaging/freebsd/icons/512x512/${PORTNAME}.png \
		${STAGEDIR}${PREFIX}/share/icons/hicolor/512x512/apps/${PORTNAME}.png
	${INSTALL_DATA} ${WRKSRC}/packaging/freebsd/icons/256x256/${PORTNAME}.png \
		${STAGEDIR}${PREFIX}/share/pixmaps/${PORTNAME}.png
	${INSTALL_DATA} ${WRKSRC}/Read-Me.md \
		${STAGEDIR}${PREFIX}/share/doc/${PORTNAME}/README.md

.include <bsd.port.post.mk>
